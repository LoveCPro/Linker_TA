#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "serialprotocol.h"
#include "log.h"
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QDateTime>
#include <QTextCursor>
#include <QtCharts/QChartView>
#include <QIcon>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QGridLayout>

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
    delete ui;
}

void MainWindow::initUI()
{
    // 设置窗口标题
    setWindowTitle("遥操作臂控制器");

    // 初始化波特率
    ui->baudRateComboBox->addItems({"115200", "256000", "921600"});
    ui->baudRateComboBox->setCurrentText("921600");

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

    calibrateTimeoutTimer->setSingleShot(true);
    connect(calibrateTimeoutTimer, &QTimer::timeout, [this]() {
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
        acceptingStream = false;
        streamEnabled = false;
        ui->armGetButton->setText("获取");
        ui->armGetButton->setStyleSheet("background-color: red; color: white;");
        setOperationButtonsEnabled(false);
        
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
            QByteArray versionBytes = payload.left(4);
            QString versionHex = versionBytes.toHex().toUpper();
            QString versionStr = QString("版本: V%1").arg(versionHex);
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
        ui->armGetButton->setText("获取");
        ui->armGetButton->setStyleSheet("background-color: #F44336; color: white;");
        logMessage("臂数据：已停止获取（不再更新界面，仍保持串口接收）");
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
    QByteArray cmd = SerialProtocol::buildCalibrateCommand();
    writeData(cmd);
    LOG_SERIAL_D("Calibrate command:" << cmd.toHex(' ').toUpper());
    startCalibrateTimeout();
}

void MainWindow::onClearLogClicked()
{
    ui->logTextEdit->clear();
}

void MainWindow::onSendCustomMessageClicked()
{
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
    if (!acceptingStream) return;

    if (leftArmData.isEmpty() || rightArmData.isEmpty()) return;

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

    // 更新状态栏
    showStatusMessage(QString("收到臂数据: 左臂%1个关节, 右臂%2个关节")
                          .arg(leftArmData.size()).arg(rightArmData.size()));
}

void MainWindow::updateCharts()
{
    if (leftArmHistory.isEmpty() || rightArmHistory.isEmpty()) return;

    // 清空现有数据
    for (auto series : leftSeries) {
        series->clear();
    }
    for (auto series : rightSeries) {
        series->clear();
    }

    // 添加新数据
    qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
    int historySize = qMin(leftArmHistory.size(), rightArmHistory.size());

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

    // 更新X轴范围（左右图表共用同一时间范围）
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
    QByteArray cmd = SerialProtocol::buildGetVersionCommand();
    writeData(cmd);
    logMessage("已发送：自动读取版本号");
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
    calibrateTimeoutTimer->start(3000);
}

void MainWindow::stopCalibrateTimeout()
{
    if (calibrateTimeoutTimer->isActive()) {
        calibrateTimeoutTimer->stop();
    }
}
