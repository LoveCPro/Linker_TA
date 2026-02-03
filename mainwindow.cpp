#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "serialprotocol.h"
#include "cancommunication.h"
#include "log.h"
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include <QTextCursor>
#include <QtCharts/QChartView>
#include <QIcon>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QGridLayout>
#include <QRegularExpressionValidator>

// 构造函数
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serialPort(new QSerialPort(this))
    , continuousTimer(new QTimer(this))
    , chartUpdateTimer(new QTimer(this))
    , armUpdateTimer(new QTimer(this))
    , versionTimeoutTimer(new QTimer(this))
    , versionRetryTimer(new QTimer(this))
    , calibrateTimeoutTimer(new QTimer(this))
    , canComm(nullptr)
    , currentMode(CommunicationMode::Serial)
    , leftArmPollTimer(new QTimer(this))
    , rightArmPollTimer(new QTimer(this))
    , bothArmsPollTimer(new QTimer(this))
    , leftArmContinuousEnabled(false)
    , rightArmContinuousEnabled(false)
    , bothArmsContinuousEnabled(false)
    , leftArmChart(new QChart())
    , rightArmChart(new QChart())
    , leftAxisX(new QDateTimeAxis())
    , leftAxisY(new QValueAxis())
    , rightAxisX(new QDateTimeAxis())
    , rightAxisY(new QValueAxis())
{
    ui->setupUi(this);

    setWindowIcon(QIcon(":/icons/app_icon.png"));

    // 初始化UI
    initUI();

    // 初始化图表
    initCharts();

    // 初始化连接
    initConnections();

    setOperationButtonsEnabled(false);

    // 刷新串口列表
    onPortsRefreshed();
}

MainWindow::~MainWindow()
{
    cleanupCANCommunication();
    delete ui;
}

void MainWindow::initUI()
{
    // 设置窗口标题
    setWindowTitle(QString("遥操臂控制器  v%1").arg(APP_VERSION));

    // 初始化波特率
    ui->baudRateComboBox->setEditable(true); // 允许手动输入
    ui->baudRateComboBox->addItems({"115200", "256000", "921600", "1000000", "2000000", "3000000"});
    ui->baudRateComboBox->setCurrentText("2000000");
    // 设置验证器，只允许输入数字
    ui->baudRateComboBox->setValidator(new QIntValidator(1200, 4000000, this));

    // 初始化数据位
    ui->dataBitsComboBox->addItems({"5", "6", "7", "8"});
    ui->dataBitsComboBox->setCurrentText("8");

    // 初始化校验位
    ui->parityComboBox->addItems({"None", "Even", "Odd", "Space", "Mark"});
    ui->parityComboBox->setCurrentIndex(0);

    // 初始化停止位
    ui->stopBitsComboBox->addItems({"1", "1.5", "2"});
    ui->stopBitsComboBox->setCurrentText("1");

    // 初始化流控制
    ui->flowControlComboBox->addItems({"None", "Hardware", "Software"});
    ui->flowControlComboBox->setCurrentIndex(0);

    // 初始化ID选择
    for (int i = 0; i < 14; ++i) {
        ui->idComboBox->addItem(QString::number(i));
    }

    // 初始化CAN ID输入验证 (Hex)
    QRegularExpression hexRegex("[0-9A-Fa-f]{1,3}");
    ui->canIdEdit->setValidator(new QRegularExpressionValidator(hexRegex, this));

    if (ui->armStopButton) {
        ui->armStopButton->hide();
    }

    ui->portComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->portComboBox->setMinimumContentsLength(15);
    ui->portComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->baudRateComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->baudRateComboBox->setMinimumContentsLength(8);
    ui->baudRateComboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    if (auto grid = qobject_cast<QGridLayout*>(ui->groupBox->layout())) {
        grid->setColumnStretch(0, 0);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(2, 0);
        grid->setColumnStretch(3, 0);
        grid->setHorizontalSpacing(8);
    }

    ui->leftArmTable->setColumnCount(2);
    ui->leftArmTable->setRowCount(7);
    QStringList leftHeaders;
    leftHeaders << "关节" << "角度(°)";
    ui->leftArmTable->setHorizontalHeaderLabels(leftHeaders);
    ui->leftArmTable->verticalHeader()->setVisible(false);
    ui->leftArmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->leftArmTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui->leftArmTable->horizontalHeader()->setStretchLastSection(true);

    ui->rightArmTable->setColumnCount(2);
    ui->rightArmTable->setRowCount(7);
    QStringList rightHeaders;
    rightHeaders << "关节" << "角度(°)";
    ui->rightArmTable->setHorizontalHeaderLabels(rightHeaders);
    ui->rightArmTable->verticalHeader()->setVisible(false);
    ui->rightArmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->rightArmTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui->rightArmTable->horizontalHeader()->setStretchLastSection(true);

    ui->leftArmTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->leftArmTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int leftRowHeight = ui->leftArmTable->verticalHeader()->defaultSectionSize();
    int leftHeight = ui->leftArmTable->horizontalHeader()->height()
                     + ui->leftArmTable->frameWidth() * 2
                     + leftRowHeight * ui->leftArmTable->rowCount();
    ui->leftArmTable->setMinimumHeight(leftHeight);
    ui->leftArmTable->setMaximumHeight(leftHeight);

    ui->rightArmTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->rightArmTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int rightRowHeight = ui->rightArmTable->verticalHeader()->defaultSectionSize();
    int rightHeight = ui->rightArmTable->horizontalHeader()->height()
                      + ui->rightArmTable->frameWidth() * 2
                      + rightRowHeight * ui->rightArmTable->rowCount();
    ui->rightArmTable->setMinimumHeight(rightHeight);
    ui->rightArmTable->setMaximumHeight(rightHeight);

    // 隐藏扭矩设置Tab
    ui->tabWidget->removeTab(2);

    // 增加双臂操作按钮及重构布局以对齐
    
    // 1. 清理旧布局和不需要的控件 (包括UI文件中定义的Label)
    // 获取所有直接子控件
    const QObjectList& children = ui->canArmControlGroup->children();
    for (QObject* obj : children) {
        if (obj->isWidgetType()) {
            QWidget* w = static_cast<QWidget*>(obj);
            // 保留需要的控件
            if (w != ui->canLeftArmSingleButton && 
                w != ui->canLeftArmContinuousButton && 
                w != ui->canRightArmSingleButton && 
                w != ui->canRightArmContinuousButton && 
                w != ui->pollIntervalSpinBox) 
            {
                // 删除其他所有控件（包括旧的Label、Spacer等如果有Widget包装）
                w->hide();
                w->deleteLater();
            }
        }
    }
    
    // 删除旧布局（如果有）
    if (ui->canArmControlGroup->layout()) {
        delete ui->canArmControlGroup->layout();
    }

    QGridLayout *newLayout = new QGridLayout();
    ui->canArmControlGroup->setLayout(newLayout);
    newLayout->setHorizontalSpacing(20); // 增加水平间距
    newLayout->setVerticalSpacing(15);   // 增加垂直间距
    newLayout->setContentsMargins(15, 25, 15, 15); // 增加边距

    // 定义按钮样式
    QString styleBlue = "QPushButton { background-color: #2196F3; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } "
                        "QPushButton:pressed { background-color: #1976D2; } "
                        "QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }";
    
    // 应用样式到现有按钮
    ui->canLeftArmSingleButton->setStyleSheet(styleBlue);
    ui->canLeftArmContinuousButton->setStyleSheet(styleBlue);
    ui->canRightArmSingleButton->setStyleSheet(styleBlue);
    ui->canRightArmContinuousButton->setStyleSheet(styleBlue);

    // 左臂控制行
    newLayout->addWidget(new QLabel("左臂:", ui->canArmControlGroup), 0, 0);
    newLayout->addWidget(ui->canLeftArmSingleButton, 0, 1);
    newLayout->addWidget(ui->canLeftArmContinuousButton, 0, 2);
    
    // 右臂控制行
    newLayout->addWidget(new QLabel("右臂:", ui->canArmControlGroup), 1, 0);
    newLayout->addWidget(ui->canRightArmSingleButton, 1, 1);
    newLayout->addWidget(ui->canRightArmContinuousButton, 1, 2);

    // 双臂控制行
    newLayout->addWidget(new QLabel("双臂 (ID0-13):", ui->canArmControlGroup), 2, 0);
    canBothArmsSingleButton = new QPushButton("单次获取", ui->canArmControlGroup);
    canBothArmsContinuousButton = new QPushButton("持续获取", ui->canArmControlGroup);
    
    // 应用样式到新按钮
    canBothArmsSingleButton->setStyleSheet(styleBlue);
    canBothArmsContinuousButton->setStyleSheet(styleBlue);

    newLayout->addWidget(canBothArmsSingleButton, 2, 1);
    newLayout->addWidget(canBothArmsContinuousButton, 2, 2);

    // 轮询间隔行
    newLayout->addWidget(new QLabel("轮询间隔(ms):", ui->canArmControlGroup), 3, 0);
    newLayout->addWidget(ui->pollIntervalSpinBox, 3, 1, 1, 2); // 跨两列

    // 添加弹簧
    newLayout->setRowStretch(4, 1);
    
    // 设置高精度定时器
    leftArmPollTimer->setTimerType(Qt::PreciseTimer);
    rightArmPollTimer->setTimerType(Qt::PreciseTimer);
    bothArmsPollTimer->setTimerType(Qt::PreciseTimer);

    // 允许更小的轮询间隔
    ui->pollIntervalSpinBox->setMinimum(1);
}

void MainWindow::initCharts()
{
    // 左臂图表
    leftArmChart->setTitle("左臂关节角度");
    leftArmChart->setAnimationOptions(QChart::SeriesAnimations);
    leftArmChart->legend()->setVisible(true);
    leftArmChart->legend()->setAlignment(Qt::AlignBottom);

    // 右臂图表
    rightArmChart->setTitle("右臂关节角度");
    rightArmChart->setAnimationOptions(QChart::SeriesAnimations);
    rightArmChart->legend()->setVisible(true);
    rightArmChart->legend()->setAlignment(Qt::AlignBottom);

    // 创建数据序列（左臂7个关节）
    QStringList leftJointNames = {
        "左肩上旋转", "左上臂右摆", "左上臂右旋",
        "左肘上摆", "左肘右旋", "左腕上摆", "左腕右摆"
    };

    for (int i = 0; i < 7; ++i) {
        QLineSeries *series = new QLineSeries();
        series->setName(leftJointNames[i]);
        leftSeries.append(series);
        leftArmChart->addSeries(series);
    }

    // 创建数据序列（右臂7个关节）
    QStringList rightJointNames = {
        "右肩上旋转", "右上臂左摆", "右上臂左旋",
        "右肘上摆", "右肘左旋", "右腕上摆", "右腕左摆"
    };

    for (int i = 0; i < 7; ++i) {
        QLineSeries *series = new QLineSeries();
        series->setName(rightJointNames[i]);
        rightSeries.append(series);
        rightArmChart->addSeries(series);
    }

    // 左臂坐标轴
    leftAxisX->setFormat("hh:mm:ss");
    leftAxisX->setTitleText("时间");
    leftAxisY->setTitleText("角度 (°)");
    leftAxisY->setRange(-180, 180);

    leftArmChart->addAxis(leftAxisX, Qt::AlignBottom);
    leftArmChart->addAxis(leftAxisY, Qt::AlignLeft);
    for (auto series : leftSeries) {
        series->attachAxis(leftAxisX);
        series->attachAxis(leftAxisY);
    }

    // 右臂坐标轴
    rightAxisX->setFormat("hh:mm:ss");
    rightAxisX->setTitleText("时间");
    rightAxisY->setTitleText("角度 (°)");
    rightAxisY->setRange(-180, 180);

    rightArmChart->addAxis(rightAxisX, Qt::AlignBottom);
    rightArmChart->addAxis(rightAxisY, Qt::AlignLeft);
    for (auto series : rightSeries) {
        series->attachAxis(rightAxisX);
        series->attachAxis(rightAxisY);
    }
}

void MainWindow::initConnections()
{
    // 串口相关
    connect(ui->refreshPortsButton, &QPushButton::clicked, this, &MainWindow::onPortsRefreshed);
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(serialPort, &QSerialPort::readyRead, this, &MainWindow::onSerialDataReceived);
    connect(serialPort, &QSerialPort::errorOccurred, this, &MainWindow::onSerialErrorOccurred);

    // 臂控制
    connect(ui->armGetButton, &QPushButton::clicked, this, &MainWindow::onArmGetClicked);

    // 其他命令
    connect(ui->calibrateButton, &QPushButton::clicked, this, &MainWindow::onCalibrateClicked);
    connect(ui->clearLogButton, &QPushButton::clicked, this, &MainWindow::onClearLogClicked);
    connect(ui->sendCustomButton, &QPushButton::clicked, this, &MainWindow::onSendCustomMessageClicked);

    // 扭矩设置
    connect(ui->torqueSetButton, &QPushButton::clicked, this, &MainWindow::onTorqueSetClicked);

    // 定时器
    connect(continuousTimer, &QTimer::timeout, this, &MainWindow::onContinuousTimer);
    connect(chartUpdateTimer, &QTimer::timeout, this, &MainWindow::updateCharts);
    chartUpdateTimer->start(1000); // 每秒更新一次图表
    connect(armUpdateTimer, &QTimer::timeout, this, &MainWindow::updateUIWithArmData);
    if (!armUpdateTimer->isActive()) {
        armUpdateTimer->start(500); // 0.5s refresh rate
    }

    versionTimeoutTimer->setSingleShot(true);
    connect(versionTimeoutTimer, &QTimer::timeout, [this]() {
        logMessage("获取版本失败：超时，未收到版本响应");
        showStatusMessage("获取版本失败：版本读取超时");
    });

    versionRetryTimer->setSingleShot(true);
    connect(versionRetryTimer, &QTimer::timeout, [this]() {
        if (versionReceived) return;
        if (versionRequestCount >= 3) return;
        sendVersionRequest();
        if (versionRequestCount < 3) {
            versionRetryTimer->start(1000);
        }
    });

    // 通信模式切换
    connect(ui->commModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onCommunicationModeChanged);

    // CAN相关连接
    connect(ui->canConnectButton, &QPushButton::clicked, this, &MainWindow::onCANConnectClicked);
    connect(ui->canLeftArmSingleButton, &QPushButton::clicked, this, &MainWindow::onCANLeftArmSingleClicked);
    connect(ui->canLeftArmContinuousButton, &QPushButton::clicked, this, &MainWindow::onCANLeftArmContinuousClicked);
    connect(ui->canRightArmSingleButton, &QPushButton::clicked, this, &MainWindow::onCANRightArmSingleClicked);
    connect(ui->canRightArmContinuousButton, &QPushButton::clicked, this, &MainWindow::onCANRightArmContinuousClicked);
    
    // 双臂操作
    if (canBothArmsSingleButton) {
        connect(canBothArmsSingleButton, &QPushButton::clicked, this, &MainWindow::onCANBothArmsSingleClicked);
        connect(canBothArmsContinuousButton, &QPushButton::clicked, this, &MainWindow::onCANBothArmsContinuousClicked);
    }

    // CAN轮询定时器
    connect(leftArmPollTimer, &QTimer::timeout, this, &MainWindow::onCANLeftArmPollTimeout);
    connect(rightArmPollTimer, &QTimer::timeout, this, &MainWindow::onCANRightArmPollTimeout);
    connect(bothArmsPollTimer, &QTimer::timeout, this, &MainWindow::onCANBothArmsPollTimeout);

    calibrateTimeoutTimer->setSingleShot(true);
    connect(calibrateTimeoutTimer, &QTimer::timeout, [this]() {
        calibrating = false;
        updateCalibrateButtonState();
        logMessage("零点标定失败：超时，未收到标定响应");
        showStatusMessage("零点标定失败：标定响应超时");
    });
}

void MainWindow::onPortsRefreshed()
{
    ui->portComboBox->clear();

    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        QString portInfo = port.portName() + " - " + port.description();
        ui->portComboBox->addItem(portInfo, port.portName());
    }

    for (int i = 0; i < ui->portComboBox->count(); ++i) {
        ui->portComboBox->setItemData(i, ui->portComboBox->itemText(i), Qt::ToolTipRole);
    }

    if (ports.isEmpty()) {
        ui->portComboBox->addItem("无可用串口");
        ui->connectButton->setEnabled(false);
    } else {
        ui->connectButton->setEnabled(true);
    }
    
    if (!serialPort->isOpen()) {
        ui->connectButton->setText("连接");
        ui->connectButton->setStyleSheet("background-color: red; color: white;");
    }
}

void MainWindow::onConnectClicked()
{
    if (serialPort->isOpen()) {
        closeSerialPort();
    } else {
        openSerialPort();
    }
}

void MainWindow::openSerialPort()
{
    if (serialPort->isOpen()) {
        serialPort->close();
    }

    QString portName = ui->portComboBox->currentData().toString();
    if (portName.isEmpty()) {
        QMessageBox::warning(this, "警告", "请选择串口");
        return;
    }

    serialPort->setPortName(portName);

    // 设置波特率
    serialPort->setBaudRate(ui->baudRateComboBox->currentText().toInt());

    // 设置数据位
    switch (ui->dataBitsComboBox->currentText().toInt()) {
    case 5: serialPort->setDataBits(QSerialPort::Data5); break;
    case 6: serialPort->setDataBits(QSerialPort::Data6); break;
    case 7: serialPort->setDataBits(QSerialPort::Data7); break;
    case 8: serialPort->setDataBits(QSerialPort::Data8); break;
    default: serialPort->setDataBits(QSerialPort::Data8); break;
    }

    // 设置校验位
    switch (ui->parityComboBox->currentIndex()) {
    case 0: serialPort->setParity(QSerialPort::NoParity); break;
    case 1: serialPort->setParity(QSerialPort::EvenParity); break;
    case 2: serialPort->setParity(QSerialPort::OddParity); break;
    case 3: serialPort->setParity(QSerialPort::SpaceParity); break;
    case 4: serialPort->setParity(QSerialPort::MarkParity); break;
    default: serialPort->setParity(QSerialPort::NoParity); break;
    }

    // 设置停止位
    switch (ui->stopBitsComboBox->currentIndex()) {
    case 0: serialPort->setStopBits(QSerialPort::OneStop); break;
    case 1: serialPort->setStopBits(QSerialPort::OneAndHalfStop); break;
    case 2: serialPort->setStopBits(QSerialPort::TwoStop); break;
    default: serialPort->setStopBits(QSerialPort::OneStop); break;
    }

    // 设置流控制
    switch (ui->flowControlComboBox->currentIndex()) {
    case 0: serialPort->setFlowControl(QSerialPort::NoFlowControl); break;
    case 1: serialPort->setFlowControl(QSerialPort::HardwareControl); break;
    case 2: serialPort->setFlowControl(QSerialPort::SoftwareControl); break;
    default: serialPort->setFlowControl(QSerialPort::NoFlowControl); break;
    }

    if (serialPort->open(QIODevice::ReadWrite)) {
        ui->connectButton->setText("断开");
        ui->connectButton->setStyleSheet("background-color: green; color: white;");
        ui->portComboBox->setEnabled(false);
        ui->baudRateComboBox->setEnabled(false);
        ui->dataBitsComboBox->setEnabled(false);
        ui->parityComboBox->setEnabled(false);
        ui->stopBitsComboBox->setEnabled(false);
        ui->flowControlComboBox->setEnabled(false);
        ui->refreshPortsButton->setEnabled(false);

        showStatusMessage("串口已连接: " + portName);
        logMessage("串口已连接: " + portName);
        setOperationButtonsEnabled(true);
        ui->armGetButton->setText("获取");
        ui->armGetButton->setStyleSheet("background-color: red; color: white;");
        versionRequestCount = 0;
        versionReceived = false;
        sendVersionRequest();
        startVersionTimeout();
        if (versionRequestCount < 3) {
            versionRetryTimer->start(1000);
        }
    } else {
        QMessageBox::critical(this, "错误", "无法打开串口: " + serialPort->errorString());
    }
}

void MainWindow::closeSerialPort()
{
    if (serialPort->isOpen()) {
        if (streamEnabled) {
            QByteArray cmd = SerialProtocol::buildDisableDataStreamCommand();
            writeData(cmd);
            logMessage("已发送：禁用遥操臂数据推送（串口断开前）");
        }

        serialPort->close();

        ui->connectButton->setText("连接");
        ui->connectButton->setStyleSheet("background-color: red; color: white;");
        ui->portComboBox->setEnabled(true);
        ui->baudRateComboBox->setEnabled(true);
        ui->dataBitsComboBox->setEnabled(true);
        ui->parityComboBox->setEnabled(true);
        ui->stopBitsComboBox->setEnabled(true);
        ui->flowControlComboBox->setEnabled(true);
        ui->refreshPortsButton->setEnabled(true);

        continuousTimer->stop();
        armUpdateTimer->stop();
        stopVersionTimeout();
        stopCalibrateTimeout();
        if (versionRetryTimer->isActive()) {
            versionRetryTimer->stop();
        }
        versionRequestCount = 0;
        versionReceived = false;
        calibrating = false;
        acceptingStream = false;
        streamEnabled = false;
        ui->armGetButton->setText("获取");
        ui->armGetButton->setStyleSheet("background-color: red; color: white;");
        setOperationButtonsEnabled(false);

        // 清空关节数据表格
        clearArmDataUI();

        // 重置版本显示
        ui->versionLabel->setText("版本: 未知");

        showStatusMessage("串口已断开");
        logMessage("串口已断开");
    }
}

void MainWindow::onSerialDataReceived()
{
    const QByteArray data = serialPort->readAll();
    if (data.isEmpty()) return;

    rxBuffer.append(data);

    // 循环拆帧：处理粘包/拆包
    while (true) {
        const auto optFrame = SerialProtocol::tryExtractFrame(rxBuffer);
        if (!optFrame.has_value()) break;

        const SerialProtocol::Frame &frame = optFrame.value();
        if (!SerialProtocol::validateFrame(frame)) {
            logMessage("收到校验失败帧，已丢弃");
            continue;
        }
        handleProtocolFrame(frame);
    }
}

void MainWindow::handleProtocolFrame(const SerialProtocol::Frame &frame)
{
    // 协议说明：推送数据为 56字节 float(小端)，无响应；响应帧数据区第1字节为结果码
    if (frame.dataLength == 56) {
        if (!acceptingStream) return;

        QVector<float> armData;
        if (SerialProtocol::parseArmData(frame.data, armData) && armData.size() == 14) {
            QByteArray raw;
            raw.append(SerialProtocol::FRAME_HEADER);
            raw.append(static_cast<char>(frame.cmdType));
            raw.append(static_cast<char>(frame.dataLength));
            raw.append(frame.data);
            raw.append(static_cast<char>(frame.checksum));
            raw.append(SerialProtocol::FRAME_TAIL);
            LOG_FRAME_D("Arm push frame:" << raw.toHex(' ').toUpper());
            processArmData(armData);
        } else {
            logMessage("推送数据解析失败（非56字节float序列）");
        }
        return;
    }

    if (frame.data.isEmpty()) {
        logMessage(QString("收到响应帧但数据区为空：cmd=0x%1")
                       .arg(frame.cmdType, 2, 16, QLatin1Char('0')).toUpper());
        return;
    }

    const quint8 result = static_cast<quint8>(frame.data.at(0));
    const QByteArray payload = frame.data.mid(1);

    const auto okFailText = [&](const QString &okText, const QString &failText) {
        if (result == SerialProtocol::RESULT_SUCCESS) {
            logMessage(okText);
            showStatusMessage(okText);
        } else if (result == SerialProtocol::RESULT_CHECKSUM_ERROR) {
            logMessage(failText + "（校验和错误）");
            showStatusMessage(failText + "（校验和错误）");
        } else if (result == SerialProtocol::RESULT_UNKNOWN_CMD) {
            logMessage(failText + "（未知命令）");
            showStatusMessage(failText + "（未知命令）");
        } else {
            logMessage(failText + QString("（结果码0x%1）").arg(result, 2, 16, QLatin1Char('0')).toUpper());
            showStatusMessage(failText);
        }
    };

    switch (static_cast<SerialProtocol::CommandType>(frame.cmdType)) {
    case SerialProtocol::CMD_CALIBRATE:
        stopCalibrateTimeout();
        calibrating = false;
        updateCalibrateButtonState();
        okFailText("零点标定成功", "零点标定失败");
        break;
    case SerialProtocol::CMD_TORQUE_CONTROL:
        okFailText("扭矩设置成功", "扭矩设置失败");
        break;
    case SerialProtocol::CMD_ENABLE_DATA_STREAM:
        if (result == SerialProtocol::RESULT_SUCCESS) {
            streamEnabled = true;
        }
        okFailText("启用遥操臂数据推送成功", "启用遥操臂数据推送失败");
        break;
    case SerialProtocol::CMD_DISABLE_DATA_STREAM:
        if (result == SerialProtocol::RESULT_SUCCESS) {
            streamEnabled = false;
        }
        okFailText("禁用遥操臂数据推送成功", "禁用遥操臂数据推送失败");
        break;
    case SerialProtocol::CMD_GET_VERSION:
        if (result == SerialProtocol::RESULT_SUCCESS && payload.size() >= 4) {
            stopVersionTimeout();
            if (versionRetryTimer->isActive()) {
                versionRetryTimer->stop();
            }
            versionReceived = true;
            QByteArray versionBytes = payload.left(qMin(4, payload.size()));
            QString versionStr = parseVersionNumber(versionBytes);
            ui->versionLabel->setText(versionStr);
            logMessage("获取版本成功: " + versionStr);
            showStatusMessage("获取版本成功: " + versionStr);
        } else {
            okFailText("获取版本成功", "获取版本失败");
        }
        break;
    case SerialProtocol::CMD_GET_ARM_DATA: {
        // 兼容“请求/响应”：结果码 + 56字节
        if (payload.size() == 56) {
            QVector<float> armData;
            if (SerialProtocol::parseArmData(payload, armData) && armData.size() == 14) {
                QByteArray raw;
                raw.append(SerialProtocol::FRAME_HEADER);
                raw.append(static_cast<char>(frame.cmdType));
                raw.append(static_cast<char>(frame.dataLength));
                raw.append(frame.data);
                raw.append(static_cast<char>(frame.checksum));
                raw.append(SerialProtocol::FRAME_TAIL);
                LOG_FRAME_D("Arm resp frame:" << raw.toHex(' ').toUpper());
                processArmData(armData);
                updateUIWithArmData();
            }
        } else {
            okFailText("获取臂数据成功", "获取臂数据失败");
        }
        break;
    }
    default:
        logMessage(QString("收到响应：cmd=0x%1 len=%2 result=0x%3 payload=%4")
                       .arg(frame.cmdType, 2, 16, QLatin1Char('0')).toUpper()
                       .arg(frame.dataLength)
                       .arg(result, 2, 16, QLatin1Char('0')).toUpper()
                       .arg(payload.toHex(' ').toUpper()));
        break;
    }
}

void MainWindow::ensureStreamEnabled()
{
    if (!serialPort->isOpen()) {
        QMessageBox::warning(this, "警告", "串口未连接");
        return;
    }
    if (streamEnabled) return;

    writeData(SerialProtocol::buildEnableDataStreamCommand());
    logMessage("已发送：启用遥操臂数据推送（等待响应）");
}

void MainWindow::onSerialErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError && error != QSerialPort::ResourceError) {
        logMessage("串口错误: " + serialPort->errorString());
        showStatusMessage("串口错误: " + serialPort->errorString());
    }

    if (error == QSerialPort::ResourceError) {
        closeSerialPort();
        clearArmDataUI(); // 断开时清空表格
        QMessageBox::critical(this, "错误", "串口资源错误: " + serialPort->errorString());
    }
}

void MainWindow::writeData(const QByteArray &data)
{
    if (serialPort->isOpen()) {
        serialPort->write(data);
        logHexData(data, true);
    } else {
        QMessageBox::warning(this, "警告", "串口未连接");
    }
}

void MainWindow::onArmGetClicked()
{
    if (!serialPort->isOpen()) {
        QMessageBox::warning(this, "警告", "串口未连接");
        return;
    }

    if (!streamEnabled) {
        ensureStreamEnabled();
        acceptingStream = true;
        updateCalibrateButtonState(); // 数据推送开启时更新校准按钮状态

        // 重置统计
        serialStartTime = QDateTime::currentMSecsSinceEpoch();
        serialRxCount = 0;

        armUpdateTimer->start(500);
        ui->armGetButton->setText("停止");
        ui->armGetButton->setStyleSheet("background-color: green; color: white;");
        logMessage("臂数据：开始获取（接收推送数据）");
        showStatusMessage("臂数据获取已开启（接收推送数据）");
    } else {
        QByteArray cmd = SerialProtocol::buildDisableDataStreamCommand();
        writeData(cmd);
        armUpdateTimer->stop();
        acceptingStream = false;
        updateCalibrateButtonState(); // 数据推送关闭时更新校准按钮状态
        
        // 计算并显示最终频率
        qint64 duration = QDateTime::currentMSecsSinceEpoch() - serialStartTime;
        double freq = 0;
        if (duration > 0) {
            freq = (double)serialRxCount * 1000.0 / duration;
        }
        
        ui->armGetButton->setText("获取");
        ui->armGetButton->setStyleSheet("background-color: #F44336; color: white;");
        logMessage(QString("臂数据：已停止获取，平均接收频率: %1 Hz").arg(freq, 0, 'f', 2));
        showStatusMessage("臂数据获取已停止");
    }
}

void MainWindow::onContinuousTimer()
{
    QByteArray cmd = SerialProtocol::buildGetArmDataCommand();
    writeData(cmd);
}

void MainWindow::onCalibrateClicked()
{
    if (currentMode == CommunicationMode::Serial) {
        QByteArray cmd = SerialProtocol::buildCalibrateCommand();
        writeData(cmd);
        LOG_SERIAL_D("Calibrate command:" << cmd.toHex(' ').toUpper());
    } else if (currentMode == CommunicationMode::CAN) {
        if (canComm && canComm->isConnected()) {
            canComm->sendCalibrate();
            logMessage("已发送校准命令 (CAN)");
        } else {
            logMessage("CAN未连接，无法发送校准命令");
            return;
        }
    }
    calibrating = true;
    updateCalibrateButtonState();
    startCalibrateTimeout();
}

void MainWindow::onClearLogClicked()
{
    ui->logTextEdit->clear();
}

void MainWindow::onSendCustomMessageClicked()
{
    if (currentMode == CommunicationMode::CAN) {
        // CAN模式发送逻辑
        QString idStr = ui->canIdEdit->text().trimmed();
        if (idStr.isEmpty()) {
            QMessageBox::warning(this, "警告", "请输入CAN ID");
            return;
        }

        bool ok;
        quint16 id = idStr.toUShort(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, "警告", "无效的CAN ID (请输入16进制数值)");
            return;
        }

        QString hexString = ui->customMessageEdit->text().trimmed();
        hexString.remove(' ');
        QByteArray data = QByteArray::fromHex(hexString.toLatin1());

        if (data.size() > 8) {
            QMessageBox::warning(this, "警告", "CAN数据不能超过8字节");
            return;
        }

        if (canComm && canComm->isConnected()) {
            canComm->sendCustomMessage(id, data);
        } else {
            QMessageBox::warning(this, "警告", "CAN未连接");
        }
    } else {
        // 串口模式发送逻辑
        QString hexString = ui->customMessageEdit->text().trimmed();
        hexString.remove(' ');

        if (hexString.isEmpty()) {
            QMessageBox::warning(this, "警告", "请输入自定义消息");
            return;
        }

        QByteArray data = QByteArray::fromHex(hexString.toLatin1());
        writeData(data);
        LOG_SERIAL_D("Custom command:" << data.toHex(' ').toUpper());
    }
}

void MainWindow::onTorqueSetClicked()
{
    quint8 id = static_cast<quint8>(ui->idComboBox->currentText().toInt());
    float speed = ui->speedSpinBox->value();
    float acceleration = ui->accelerationSpinBox->value();
    float torque = ui->torqueSpinBox->value();
    float position = ui->positionSpinBox->value();

    // 构建扭矩控制命令
    QByteArray cmd = SerialProtocol::buildTorqueControlCommand(id, speed, acceleration, torque, position);
    writeData(cmd);

    logMessage(QString("扭矩设置: ID=%1, 位置=%2, 速度=%3, 加速度=%4, 扭矩=%5")
                   .arg(id).arg(position).arg(speed).arg(acceleration).arg(torque));
    LOG_SERIAL_D("Torque command:" << cmd.toHex(' ').toUpper());
}

void MainWindow::processArmData(const QVector<float> &armData)
{
    if (armData.size() < 14) return;

    // 更新无线接收计数
    if (currentMode == CommunicationMode::Serial && acceptingStream) {
        serialRxCount++;
    }

    // 分离左右臂数据
    leftArmData.clear();
    rightArmData.clear();

    for (int i = 0; i < 7; ++i) {
        leftArmData.append(armData[i]);
    }

    for (int i = 7; i < 14; ++i) {
        rightArmData.append(armData[i]);
    }

    // 记录历史数据用于图表
    qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();

    static const int MAX_HISTORY = 100;
    if (leftArmHistory.size() >= MAX_HISTORY) {
        leftArmHistory.removeFirst();
    }
    if (rightArmHistory.size() >= MAX_HISTORY) {
        rightArmHistory.removeFirst();
    }

    leftArmHistory.append(leftArmData);
    rightArmHistory.append(rightArmData);
}

void MainWindow::updateUIWithArmData()
{
    // 仅在串口模式下检查 acceptingStream
    if (currentMode == CommunicationMode::Serial && !acceptingStream) return;

    // 分别更新左臂和右臂数据，不强制要求两者都有

    if (!leftArmData.isEmpty()) {
        QStringList leftNames = {
            "旋转",
            "右摆",
            "右旋转",
            "上摆",
            "右旋转",
            "上摆",
            "右摆"
        };

        int leftCount = qMin(leftArmData.size(), leftNames.size());
        for (int i = 0; i < leftCount; ++i) {
            QString label = QString("ID%1(%2)").arg(i).arg(leftNames[i]);
            if (!ui->leftArmTable->item(i, 0)) {
                ui->leftArmTable->setItem(i, 0, new QTableWidgetItem());
            }
            if (!ui->leftArmTable->item(i, 1)) {
                ui->leftArmTable->setItem(i, 1, new QTableWidgetItem());
            }
            ui->leftArmTable->item(i, 0)->setText(label);
            ui->leftArmTable->item(i, 1)->setText(QString::number(leftArmData[i], 'f', 2));
        }
    }

    if (!rightArmData.isEmpty()) {
        QStringList rightNames = {
            "旋转",
            "左摆",
            "左旋转",
            "上摆",
            "左旋转",
            "上摆",
            "左摆"
        };

        int rightCount = qMin(rightArmData.size(), rightNames.size());
        for (int i = 0; i < rightCount; ++i) {
            int id = 7 + i;
            QString label = QString("ID%1(%2)").arg(id).arg(rightNames[i]);
            if (!ui->rightArmTable->item(i, 0)) {
                ui->rightArmTable->setItem(i, 0, new QTableWidgetItem());
            }
            if (!ui->rightArmTable->item(i, 1)) {
                ui->rightArmTable->setItem(i, 1, new QTableWidgetItem());
            }
            ui->rightArmTable->item(i, 0)->setText(label);
            ui->rightArmTable->item(i, 1)->setText(QString::number(rightArmData[i], 'f', 2));
        }
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    double sendFreq = 0.0;
    double recvFreq = 0.0;
    bool hasDataTransfer = false; // 是否有数据传输

    if (leftArmContinuousEnabled && leftSendStartTime > 0 && leftArmStartTime > 0) {
        qint64 sd = now - leftSendStartTime;
        qint64 rd = now - leftArmStartTime;
        if (sd > 0) sendFreq = (double)leftSendCount * 1000.0 / sd;
        if (rd > 0) recvFreq = (double)leftArmFrameCount * 1000.0 / rd;
        hasDataTransfer = true;
    } else if (rightArmContinuousEnabled && rightSendStartTime > 0 && rightArmStartTime > 0) {
        qint64 sd = now - rightSendStartTime;
        qint64 rd = now - rightArmStartTime;
        if (sd > 0) sendFreq = (double)rightSendCount * 1000.0 / sd;
        if (rd > 0) recvFreq = (double)rightArmFrameCount * 1000.0 / rd;
        hasDataTransfer = true;
    } else if (bothArmsContinuousEnabled && bothSendStartTime > 0 && bothArmsStartTime > 0) {
        qint64 sd = now - bothSendStartTime;
        qint64 rd = now - bothArmsStartTime;
        if (sd > 0) sendFreq = (double)bothSendCount * 1000.0 / sd;
        if (rd > 0) recvFreq = (double)bothArmsFrameCount * 1000.0 / rd;
        hasDataTransfer = true;
    } else if (currentMode == CommunicationMode::Serial && acceptingStream && serialStartTime > 0) {
        qint64 rd = now - serialStartTime;
        // 串口模式下如果是数据流推送，发送频率视为0（或者统计心跳包，这里暂定为0）
        sendFreq = 0.0;
        if (rd > 0) recvFreq = (double)serialRxCount * 1000.0 / rd;
        hasDataTransfer = true;
    }

    // 在状态栏显示频率信息
    if (currentMode == CommunicationMode::Serial) {
        // 无线模式只显示接收频率
        showStatusMessage(QString("接收频率: %1 Hz").arg(recvFreq, 0, 'f', 2));
    } else {
        // CAN模式显示发送和接收频率
        showStatusMessage(QString("发送频率: %1 Hz, 接收频率: %2 Hz").arg(sendFreq, 0, 'f', 2).arg(recvFreq, 0, 'f', 2));
    }
}

void MainWindow::updateCharts()
{
    if (leftArmHistory.isEmpty() && rightArmHistory.isEmpty()) return;

    // 清空现有数据
    for (auto series : leftSeries) {
        series->clear();
    }
    for (auto series : rightSeries) {
        series->clear();
    }

    // 添加新数据
    qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
    int historySize = qMax(leftArmHistory.size(), rightArmHistory.size());

    for (int i = 0; i < historySize; ++i) {
        qint64 time = currentTime - (historySize - i) * 1000; // 假设每秒一个数据点

        // 左臂数据
        if (i < leftArmHistory.size()) {
            const QVector<float> &leftData = leftArmHistory[i];
            for (int j = 0; j < qMin(leftSeries.size(), leftData.size()); ++j) {
                leftSeries[j]->append(time, leftData[j]);
            }
        }

        // 右臂数据
        if (i < rightArmHistory.size()) {
            const QVector<float> &rightData = rightArmHistory[i];
            for (int j = 0; j < qMin(rightSeries.size(), rightData.size()); ++j) {
                rightSeries[j]->append(time, rightData[j]);
            }
        }
    }

    // 更新X轴范围
    qint64 minTime = currentTime - historySize * 1000;
    qint64 maxTime = currentTime;
    leftAxisX->setRange(QDateTime::fromMSecsSinceEpoch(minTime),
                        QDateTime::fromMSecsSinceEpoch(maxTime));
    rightAxisX->setRange(QDateTime::fromMSecsSinceEpoch(minTime),
                         QDateTime::fromMSecsSinceEpoch(maxTime));
}

void MainWindow::logMessage(const QString &message)
{
    QString timestamp = getCurrentTimeString();
    ui->logTextEdit->append(timestamp + " " + message);

    // 自动滚动到底部
    QTextCursor cursor = ui->logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->logTextEdit->setTextCursor(cursor);
}

void MainWindow::logHexData(const QByteArray &data, bool isSend)
{
    QString direction = isSend ? "发送" : "接收";
    QString hexString = data.toHex(' ').toUpper();
    QString prefix = direction + "数据: ";

    if (data.size() >= 3 &&
        static_cast<quint8>(data.at(0)) == static_cast<quint8>(SerialProtocol::FRAME_HEADER) &&
        static_cast<quint8>(data.at(data.size() - 1)) == static_cast<quint8>(SerialProtocol::FRAME_TAIL)) {
        quint8 cmd = static_cast<quint8>(data.at(1));
        QString cmdName;
        switch (cmd) {
        case SerialProtocol::CMD_GET_ARM_DATA: cmdName = "GET_ARM_DATA"; break;
        case SerialProtocol::CMD_GET_VERSION: cmdName = "获取版本号"; break;
        case SerialProtocol::CMD_ENABLE_DATA_STREAM: cmdName = "开启摇操臂数据推送"; break;
        case SerialProtocol::CMD_DISABLE_DATA_STREAM: cmdName = "禁止摇操臂数据推送"; break;
        case SerialProtocol::CMD_CALIBRATE: cmdName = "零点标定"; break;
        case SerialProtocol::CMD_TORQUE_CONTROL: cmdName = "扭矩设置"; break;
        case SerialProtocol::CMD_SET_PARAMS: cmdName = "SET_PARAMS"; break;
        default: cmdName = "UNKNOWN"; break;
        }
        prefix += QString("[0x%1 %2] ")
                      .arg(cmd, 2, 16, QLatin1Char('0')).toUpper()
                      .arg(cmdName);
    }

    logMessage(prefix + hexString);
}

QString MainWindow::getCurrentTimeString()
{
    return QDateTime::currentDateTime().toString("[hh:mm:ss.zzz]");
}

QString MainWindow::parseVersionNumber(const QByteArray &versionBytes)
{
    if (versionBytes.isEmpty()) {
        return "版本: 未知";
    }

    // 解析版本号字节（固定4字节格式）
    // 格式: [硬件版本] [软件版本] [保留1] [保留2]
    // 示例: 72 64 01 00 -> 硬件版本: V1.1.4, 软件版本: V1.0.0
    QString hwVersion = "未知";
    QString swVersion = "未知";

    // 辅助函数：将数字转换为X.Y.Z格式
    auto formatVersion = [](quint8 v) -> QString {
        int major = v / 100;
        int minor = (v % 100) / 10;
        int patch = v % 10;
        return QString("V%1.%2.%3").arg(major).arg(minor).arg(patch);
    };

    // 硬件版本号 (Byte 0)
    if (versionBytes.size() >= 1) {
        quint8 hw = static_cast<quint8>(versionBytes[0]);
        hwVersion = formatVersion(hw);
    }

    // 软件版本号 (Byte 1)
    if (versionBytes.size() >= 2) {
        quint8 sw = static_cast<quint8>(versionBytes[1]);
        swVersion = formatVersion(sw);
    }

    QString versionStr = QString("硬件版本: %1, 软件版本: %2").arg(hwVersion).arg(swVersion);
    return versionStr;
}

void MainWindow::showStatusMessage(const QString &message, int timeout)
{
    statusBar()->showMessage(message, timeout);
}

void MainWindow::setOperationButtonsEnabled(bool enabled)
{
    ui->armGetButton->setEnabled(enabled);
    ui->calibrateButton->setEnabled(enabled);
    ui->sendCustomButton->setEnabled(enabled);
    ui->torqueSetButton->setEnabled(enabled);
}

void MainWindow::sendVersionRequest()
{
    if (currentMode == CommunicationMode::Serial) {
        if (serialPort->isOpen()) {
            QByteArray cmd = SerialProtocol::buildGetVersionCommand();
            writeData(cmd);
            logMessage("已发送：自动读取版本号 (串口)");
        }
    } else if (currentMode == CommunicationMode::CAN) {
        if (canComm && canComm->isConnected()) {
            canComm->sendGetVersion();
            logMessage("已发送：自动读取版本号 (CAN)");
        }
    }
    versionRequestCount++;
}

void MainWindow::startVersionTimeout()
{
    versionTimeoutTimer->start(3000);
}

void MainWindow::stopVersionTimeout()
{
    if (versionTimeoutTimer->isActive()) {
        versionTimeoutTimer->stop();
    }
}

void MainWindow::startCalibrateTimeout()
{
    // 无线模式响应较慢，使用更长的超时时间
    int timeout = (currentMode == CommunicationMode::Serial) ? 10000 : 3000;
    calibrateTimeoutTimer->start(timeout);
}

void MainWindow::stopCalibrateTimeout()
{
    if (calibrateTimeoutTimer->isActive()) {
        calibrateTimeoutTimer->stop();
    }
}

void MainWindow::updateCalibrateButtonState()
{
    // 校准中或数据获取中时禁用校准按钮
    bool enabled = !calibrating;
    // 无线模式：数据推送开启时禁用
    if (currentMode == CommunicationMode::Serial && acceptingStream) {
        enabled = false;
    }
    // CAN模式：持续获取开启时禁用
    else if (currentMode == CommunicationMode::CAN) {
        if (leftArmContinuousEnabled || rightArmContinuousEnabled || bothArmsContinuousEnabled) {
            enabled = false;
        }
    }
    ui->calibrateButton->setEnabled(enabled);
}

// ==================== CAN通信相关实现 ====================

void MainWindow::initCANCommunication()
{
    if (!canComm) {
        canComm = new CANCommunication(this);

        // 连接CAN信号
        connect(canComm, &CANCommunication::statusChanged, this, &MainWindow::onCANStatusChanged);
        connect(canComm, &CANCommunication::leftArmDataReceived, this, &MainWindow::onCANLeftArmDataReceived);
        connect(canComm, &CANCommunication::rightArmDataReceived, this, &MainWindow::onCANRightArmDataReceived);
        connect(canComm, &CANCommunication::logMessage, this, &MainWindow::onCANLogMessage);
        connect(canComm, &CANCommunication::errorOccurred, this, &MainWindow::onCANErrorOccurred);
        connect(canComm, &CANCommunication::calibrationResultReceived, [this](bool success) {
            stopCalibrateTimeout();
            calibrating = false;
            updateCalibrateButtonState();
            if (success) {
                logMessage("零点标定成功 (CAN)");
                showStatusMessage("零点标定成功 (CAN)");
            } else {
                logMessage("零点标定失败 (CAN)");
                showStatusMessage("零点标定失败 (CAN)");
            }
        });
        connect(canComm, &CANCommunication::versionReceived, [this](const QString &version) {
            ui->versionLabel->setText("版本: " + version);
            logMessage("获取版本成功 (CAN): " + version);
            showStatusMessage("获取版本成功 (CAN): " + version);
            stopVersionTimeout();
            if (versionRetryTimer->isActive()) {
                versionRetryTimer->stop();
            }
            versionReceived = true;
        });
    }
}

void MainWindow::cleanupCANCommunication()
{
    stopCANPolling();

    if (canComm) {
        if (canComm->isConnected()) {
            canComm->disconnect();
        }
        delete canComm;
        canComm = nullptr;
    }

    leftArmContinuousEnabled = false;
    rightArmContinuousEnabled = false;
}

void MainWindow::onCommunicationModeChanged(int index)
{
    CommunicationMode newMode = static_cast<CommunicationMode>(index);

    if (newMode == currentMode) {
        return;
    }

    // 断开现有连接
    if (currentMode == CommunicationMode::Serial && serialPort->isOpen()) {
        closeSerialPort();
    } else if (currentMode == CommunicationMode::CAN && canComm && canComm->isConnected()) {
        canComm->disconnect();
    }

    currentMode = newMode;
    updateUIForCommunicationMode(newMode);

    QString modeName = (newMode == CommunicationMode::Serial) ? "无线摇操臂 (串口)" : "有线摇操臂 (CAN)";
    logMessage(QString("切换通信模式: %1").arg(modeName));
    showStatusMessage(QString("已切换到%1模式").arg(modeName));
}

void MainWindow::updateUIForCommunicationMode(CommunicationMode mode)
{
    if (mode == CommunicationMode::Serial) {
        // 显示串口控件，隐藏CAN控件
        ui->groupBox->setVisible(true);
        ui->canGroup->setVisible(false);
        ui->armControlGroup->setVisible(true);
        ui->canArmControlGroup->setVisible(false);

        // 隐藏CAN ID输入，恢复标签
        ui->canIdLabel->setVisible(false);
        ui->canIdEdit->setVisible(false);
        ui->label_13->setText("发送自定义消息:");

        enableSerialControls(true);
        enableCANControls(false);
    } else {
        // 隐藏串口控件，显示CAN控件
        ui->groupBox->setVisible(false);
        ui->canGroup->setVisible(true);
        ui->armControlGroup->setVisible(false);
        ui->canArmControlGroup->setVisible(true);

        // 显示CAN ID输入，更改标签
        ui->canIdLabel->setVisible(true);
        ui->canIdEdit->setVisible(true);
        ui->label_13->setText("数据 (Hex):");

        enableSerialControls(false);
        enableCANControls(true);
    }
}

void MainWindow::enableSerialControls(bool enabled)
{
    ui->portComboBox->setEnabled(enabled && !serialPort->isOpen());
    ui->baudRateComboBox->setEnabled(enabled && !serialPort->isOpen());
    ui->dataBitsComboBox->setEnabled(enabled && !serialPort->isOpen());
    ui->parityComboBox->setEnabled(enabled && !serialPort->isOpen());
    ui->stopBitsComboBox->setEnabled(enabled && !serialPort->isOpen());
    ui->flowControlComboBox->setEnabled(enabled && !serialPort->isOpen());
    ui->refreshPortsButton->setEnabled(enabled && !serialPort->isOpen());
}

void MainWindow::enableCANControls(bool enabled)
{
    bool isConnected = canComm && canComm->isConnected();
    ui->canConnectButton->setEnabled(enabled);
    ui->canLeftArmSingleButton->setEnabled(enabled && isConnected);
    ui->canRightArmSingleButton->setEnabled(enabled && isConnected);
    ui->canLeftArmContinuousButton->setEnabled(enabled && isConnected);
    ui->canRightArmContinuousButton->setEnabled(enabled && isConnected);
    
    if (canBothArmsSingleButton) {
        canBothArmsSingleButton->setEnabled(enabled && isConnected);
        canBothArmsContinuousButton->setEnabled(enabled && isConnected);
    }
}

void MainWindow::onCANConnectClicked()
{
    initCANCommunication();

    if (canComm->isConnected()) {
        canComm->disconnect();
        ui->canConnectButton->setText("连接CAN");
        ui->canStatusLabel->setText("CAN状态: 未连接");
        enableCANControls(true);
        logMessage("CAN已断开");
    } else {
        canComm->connect("PCAN_USBBUS1", 1000000);
        // 连接结果在onCANStatusChanged中处理
    }
}

void MainWindow::onCANStatusChanged(int status)
{
    if (status == static_cast<int>(CANCommunication::Connected)) {
        ui->canConnectButton->setText("断开CAN");
        ui->canStatusLabel->setText("CAN状态: 已连接");
        ui->canConnectButton->setStyleSheet("background-color: green; color: white;");
        enableCANControls(true);
        setOperationButtonsEnabled(true);
        showStatusMessage("CAN连接成功");
        
        // 连接成功后尝试获取版本
        versionRequestCount = 0;
        versionReceived = false;
        sendVersionRequest();
        startVersionTimeout();
        if (versionRequestCount < 3) {
            versionRetryTimer->start(1000);
        }
    } else {
        ui->canConnectButton->setText("连接CAN");
        ui->canStatusLabel->setText("CAN状态: 未连接");
        ui->canConnectButton->setStyleSheet("background-color: red; color: white;");
        stopCANPolling();
        enableCANControls(true);
        setOperationButtonsEnabled(false);
        clearArmDataUI(); // 断开时清空表格
        // 重置版本显示
        ui->versionLabel->setText("版本: 未知");
        showStatusMessage("CAN已断开");
    }
}

void MainWindow::onCANLeftArmSingleClicked()
{
    if (!canComm || !canComm->isConnected()) {
        logMessage("CAN未连接，无法获取左臂数据");
        return;
    }

    logMessage("发送左臂单次获取请求");
    canComm->sendRequest(CANCommunication::LeftArm);
}

void MainWindow::onCANLeftArmContinuousClicked()
{
    if (!canComm || !canComm->isConnected()) {
        logMessage("CAN未连接，无法启动左臂持续获取");
        return;
    }

    if (leftArmContinuousEnabled) {
        // 停止
        leftArmPollTimer->stop();
        leftArmContinuousEnabled = false;
        ui->canLeftArmContinuousButton->setText("持续获取");
        ui->canLeftArmContinuousButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #1976D2; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }");
        ui->canLeftArmSingleButton->setEnabled(true);
        
        // 计算并显示频率
        qint64 duration = QDateTime::currentMSecsSinceEpoch() - leftArmStartTime;
        double freq = 0;
        if (duration > 0) {
            freq = (double)leftArmFrameCount * 1000.0 / duration;
        }
        logMessage(QString("左臂持续获取已停止, 平均频率: %1 Hz").arg(freq, 0, 'f', 2));
        updateCalibrateButtonState();
    } else {
        // 启动
        stopCANPolling();
        int interval = ui->pollIntervalSpinBox->value();
        leftArmPollTimer->start(interval);
        leftArmContinuousEnabled = true;
        ui->canLeftArmContinuousButton->setText("停止持续");
        ui->canLeftArmContinuousButton->setStyleSheet("QPushButton { background-color: #F44336; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #D32F2F; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }");
        ui->canLeftArmSingleButton->setEnabled(false);
        
        // 重置统计
        leftArmStartTime = QDateTime::currentMSecsSinceEpoch();
        leftArmFrameCount = 0;
        leftSendStartTime = leftArmStartTime;
        leftSendCount = 0;

        logMessage(QString("左臂持续获取已启动 (间隔: %1ms)").arg(interval));
        updateCalibrateButtonState();
    }
}

void MainWindow::onCANLeftArmPollTimeout()
{
    if (canComm && canComm->isConnected()) {
        canComm->sendRequest(CANCommunication::LeftArm);
        leftSendCount++;
    }
}

void MainWindow::onCANRightArmSingleClicked()
{
    if (!canComm || !canComm->isConnected()) {
        logMessage("CAN未连接，无法获取右臂数据");
        return;
    }

    logMessage("发送右臂单次获取请求");
    canComm->sendRequest(CANCommunication::RightArm);
}

void MainWindow::onCANRightArmContinuousClicked()
{
    if (!canComm || !canComm->isConnected()) {
        logMessage("CAN未连接，无法启动右臂持续获取");
        return;
    }

    if (rightArmContinuousEnabled) {
        // 停止
        rightArmPollTimer->stop();
        rightArmContinuousEnabled = false;
        ui->canRightArmContinuousButton->setText("持续获取");
        ui->canRightArmContinuousButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #1976D2; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }");
        ui->canRightArmSingleButton->setEnabled(true);
        
        // 计算并显示频率
        qint64 duration = QDateTime::currentMSecsSinceEpoch() - rightArmStartTime;
        double freq = 0;
        if (duration > 0) {
            freq = (double)rightArmFrameCount * 1000.0 / duration;
        }
        logMessage(QString("右臂持续获取已停止, 平均频率: %1 Hz").arg(freq, 0, 'f', 2));
        updateCalibrateButtonState();
    } else {
        // 启动
        stopCANPolling();
        int interval = ui->pollIntervalSpinBox->value();
        rightArmPollTimer->start(interval);
        rightArmContinuousEnabled = true;
        ui->canRightArmContinuousButton->setText("停止持续");
        ui->canRightArmContinuousButton->setStyleSheet("QPushButton { background-color: #F44336; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #D32F2F; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }");
        ui->canRightArmSingleButton->setEnabled(false);
        
        // 重置统计
        rightArmStartTime = QDateTime::currentMSecsSinceEpoch();
        rightArmFrameCount = 0;
        rightSendStartTime = rightArmStartTime;
        rightSendCount = 0;

        logMessage(QString("右臂持续获取已启动 (间隔: %1ms)").arg(interval));
        updateCalibrateButtonState();
    }
}

void MainWindow::onCANRightArmPollTimeout()
{
    if (canComm && canComm->isConnected()) {
        canComm->sendRequest(CANCommunication::RightArm);
        rightSendCount++;
    }
}

void MainWindow::onCANBothArmsSingleClicked()
{
    if (!canComm || !canComm->isConnected()) {
        logMessage("CAN未连接，无法获取双臂数据");
        return;
    }

    logMessage("发送双臂单次获取请求");
    canComm->sendRequest(CANCommunication::BothArms);
}

void MainWindow::onCANBothArmsContinuousClicked()
{
    if (!canComm || !canComm->isConnected()) {
        logMessage("CAN未连接，无法启动双臂持续获取");
        return;
    }

    if (bothArmsContinuousEnabled) {
        // 停止
        bothArmsPollTimer->stop();
        bothArmsContinuousEnabled = false;
        if (canBothArmsContinuousButton) {
            canBothArmsContinuousButton->setText("持续获取");
            canBothArmsContinuousButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #1976D2; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }");
            canBothArmsSingleButton->setEnabled(true);
        }
        
        // 计算并显示频率
        qint64 duration = QDateTime::currentMSecsSinceEpoch() - bothArmsStartTime;
        double freq = 0;
        if (duration > 0) {
            freq = (double)bothArmsFrameCount * 1000.0 / duration;
        }
        logMessage(QString("双臂持续获取已停止, 平均频率: %1 Hz").arg(freq, 0, 'f', 2));
        updateCalibrateButtonState();
    } else {
        // 启动
        stopCANPolling();
        int interval = ui->pollIntervalSpinBox->value();
        bothArmsPollTimer->start(interval);
        bothArmsContinuousEnabled = true;
        if (canBothArmsContinuousButton) {
            canBothArmsContinuousButton->setText("停止持续");
            canBothArmsContinuousButton->setStyleSheet("QPushButton { background-color: #F44336; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #D32F2F; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }");
            canBothArmsSingleButton->setEnabled(false);
        }
        
        // 重置统计
        bothArmsStartTime = QDateTime::currentMSecsSinceEpoch();
        bothArmsFrameCount = 0;
        bothSendStartTime = bothArmsStartTime;
        bothSendCount = 0;

        logMessage(QString("双臂持续获取已启动 (间隔: %1ms)").arg(interval));
        updateCalibrateButtonState();
    }
}

void MainWindow::onCANBothArmsPollTimeout()
{
    if (canComm && canComm->isConnected()) {
        canComm->sendRequest(CANCommunication::BothArms);
        bothSendCount++;
    }
}

void MainWindow::clearArmDataUI()
{
    // 清空表格内容
    ui->leftArmTable->clearContents();
    ui->rightArmTable->clearContents();
    
    // 清空数据缓存
    leftArmData.clear();
    rightArmData.clear();
    
    // 清空图表
    for (auto series : leftSeries) {
        series->clear();
    }
    for (auto series : rightSeries) {
        series->clear();
    }
}
void MainWindow::stopCANPolling()
{
    leftArmPollTimer->stop();
    rightArmPollTimer->stop();
    bothArmsPollTimer->stop();
    leftArmContinuousEnabled = false;
    rightArmContinuousEnabled = false;
    bothArmsContinuousEnabled = false;

    // Reset buttons states
    QString styleBlue = "QPushButton { background-color: #2196F3; color: white; border-radius: 4px; padding: 6px; font-weight: bold; } QPushButton:pressed { background-color: #1976D2; } QPushButton:disabled { background-color: #E0E0E0; color: #A0A0A0; }";

    ui->canLeftArmContinuousButton->setText("持续获取");
    ui->canLeftArmContinuousButton->setStyleSheet(styleBlue);
    ui->canLeftArmSingleButton->setEnabled(true);
    
    ui->canRightArmContinuousButton->setText("持续获取");
    ui->canRightArmContinuousButton->setStyleSheet(styleBlue);
    ui->canRightArmSingleButton->setEnabled(true);

    if (canBothArmsContinuousButton) {
        canBothArmsContinuousButton->setText("持续获取");
        canBothArmsContinuousButton->setStyleSheet(styleBlue);
        canBothArmsSingleButton->setEnabled(true);
    }
}

void MainWindow::onCANLeftArmDataReceived(const QVector<float> &data)
{
    if (data.size() != 7) {
        logMessage(QString("左臽数据格式错误: 期望7个关节, 收到%1个").arg(data.size()));
        return;
    }

    // 更新左臂数据
    leftArmData.clear();
    for (int i = 0; i < 7; ++i) {
        leftArmData.append(data[i]);
    }

    // 记录历史数据
    qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
    static const int MAX_HISTORY = 100;
    if (leftArmHistory.size() >= MAX_HISTORY) {
        leftArmHistory.removeFirst();
    }
    leftArmHistory.append(leftArmData);

    // 更新UI
    // 单次获取时立即更新表格，持续获取时由定时器更新避免频闪
    if (!leftArmContinuousEnabled && !rightArmContinuousEnabled && !bothArmsContinuousEnabled) {
        updateUIWithArmData();
    }

    // 持续模式时立即更新图表
    if (leftArmContinuousEnabled || rightArmContinuousEnabled) {
        updateCharts();
    }

    if (leftArmContinuousEnabled) {
        leftArmFrameCount++;
    } else if (bothArmsContinuousEnabled) {
        bothArmsFrameCount++;
    }

    // 格式化完整日志
    QString logStr = "收到左臂数据: ";
    for (int i = 0; i < data.size(); ++i) {
        logStr += QString::number(data[i], 'f', 2);
        if (i < data.size() - 1) logStr += ", ";
    }
    logMessage(logStr);
}

void MainWindow::onCANRightArmDataReceived(const QVector<float> &data)
{
    if (data.size() != 7) {
        logMessage(QString("右臂数据格式错误: 期望7个关节, 收到%1个").arg(data.size()));
        return;
    }

    // 更新右臂数据
    rightArmData.clear();
    for (int i = 0; i < 7; ++i) {
        rightArmData.append(data[i]);
    }

    // 记录历史数据
    qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
    static const int MAX_HISTORY = 100;
    if (rightArmHistory.size() >= MAX_HISTORY) {
        rightArmHistory.removeFirst();
    }
    rightArmHistory.append(rightArmData);

    // 更新UI
    // 单次获取时立即更新表格，持续获取时由定时器更新避免频闪
    if (!leftArmContinuousEnabled && !rightArmContinuousEnabled && !bothArmsContinuousEnabled) {
        updateUIWithArmData();
    }

    // 持续模式时立即更新图表
    if (leftArmContinuousEnabled || rightArmContinuousEnabled) {
        updateCharts();
    }

    if (rightArmContinuousEnabled) {
        rightArmFrameCount++;
    } else if (rightArmContinuousEnabled) {
        rightArmFrameCount++;
    } else if (bothArmsContinuousEnabled) {
        // 双臂模式下也要统计右臂接收（通常左右臂一起回，这里做个冗余或者按需统计，
        // 前面 updateUIWithArmData 是统一计算频率，这里可以不加，
        // 但为了日志或者其他逻辑一致性，保持现状或仅在独立模式统计）
        // 注意：bothArmsFrameCount 在左臂回调里加了，这里不需要再加，否则频率会翻倍
    }

    // 格式化完整日志
    QString logStr = "收到右臂数据: ";
    for (int i = 0; i < data.size(); ++i) {
        logStr += QString::number(data[i], 'f', 2);
        if (i < data.size() - 1) logStr += ", ";
    }
    logMessage(logStr);
}

void MainWindow::onCANLogMessage(const QString &message, const QString &type)
{
    // 根据类型添加颜色标识
    QString color;
    if (type == "error") {
        color = "red";
    } else if (type == "success") {
        color = "green";
    } else if (type == "warning") {
        color = "orange";
    } else if (type == "response") {
        color = "blue";
    } else {
        logMessage(message);
        return;
    }

    QString timestamp = getCurrentTimeString();
    ui->logTextEdit->append(QString("<span style='color:%1'>%2 %3</span>").arg(color).arg(timestamp).arg(message));

    QTextCursor cursor = ui->logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->logTextEdit->setTextCursor(cursor);
}

void MainWindow::onCANErrorOccurred(const QString &error)
{
    logMessage("CAN错误: " + error);
    showStatusMessage("CAN错误: " + error);
}
