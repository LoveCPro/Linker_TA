#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "serialprotocol.h"
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QDateTime>
#include <QTextCursor>
#include <QtCharts/QChartView>
#include <QIcon>

// 构造函数
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serialPort(new QSerialPort(this))
    , continuousTimer(new QTimer(this))
    , chartUpdateTimer(new QTimer(this))
    , versionTimeoutTimer(new QTimer(this))
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

    // 初始化发送间隔
    ui->sendIntervalSpinBox->setRange(1, 10000);
    ui->sendIntervalSpinBox->setValue(15);

    // 初始化ID选择
    for (int i = 0; i < 14; ++i) {
        ui->idComboBox->addItem(QString::number(i));
    }
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

    // 左臂控制
    connect(ui->leftSingleButton, &QPushButton::clicked, this, &MainWindow::onLeftSingleGetClicked);
    connect(ui->leftContinuousButton, &QPushButton::clicked, this, &MainWindow::onLeftContinuousGetClicked);
    connect(ui->leftStopButton, &QPushButton::clicked, this, &MainWindow::onLeftStopClicked);

    // 右臂控制
    connect(ui->rightSingleButton, &QPushButton::clicked, this, &MainWindow::onRightSingleGetClicked);
    connect(ui->rightContinuousButton, &QPushButton::clicked, this, &MainWindow::onRightContinuousGetClicked);
    connect(ui->rightStopButton, &QPushButton::clicked, this, &MainWindow::onRightStopClicked);

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

    versionTimeoutTimer->setSingleShot(true);
    connect(versionTimeoutTimer, &QTimer::timeout, [this]() {
        logMessage("获取版本失败：超时，未收到版本响应");
        showStatusMessage("获取版本失败：版本读取超时");
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

    if (ports.isEmpty()) {
        ui->portComboBox->addItem("无可用串口");
        ui->connectButton->setEnabled(false);
    } else {
        ui->connectButton->setEnabled(true);
    }
    
    // 初始化连接按钮状态
    if (!serialPort->isOpen()) {
        ui->connectButton->setText("连接");
        ui->connectButton->setStyleSheet("color: red;");
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
        ui->connectButton->setStyleSheet("color: green;");
        ui->portComboBox->setEnabled(false);
        ui->baudRateComboBox->setEnabled(false);
        ui->dataBitsComboBox->setEnabled(false);
        ui->parityComboBox->setEnabled(false);
        ui->stopBitsComboBox->setEnabled(false);
        ui->flowControlComboBox->setEnabled(false);
        ui->refreshPortsButton->setEnabled(false);

        showStatusMessage("串口已连接: " + portName);
        logMessage("串口已连接: " + portName);
        
        // 自动读取版本号
        QByteArray cmd = SerialProtocol::buildGetVersionCommand();
        writeData(cmd);
        logMessage("已发送：自动读取版本号");
        startVersionTimeout();
    } else {
        QMessageBox::critical(this, "错误", "无法打开串口: " + serialPort->errorString());
    }
}

void MainWindow::closeSerialPort()
{
    if (serialPort->isOpen()) {
        serialPort->close();

        ui->connectButton->setText("连接");
        ui->connectButton->setStyleSheet("color: red;");
        ui->portComboBox->setEnabled(true);
        ui->baudRateComboBox->setEnabled(true);
        ui->dataBitsComboBox->setEnabled(true);
        ui->parityComboBox->setEnabled(true);
        ui->stopBitsComboBox->setEnabled(true);
        ui->flowControlComboBox->setEnabled(true);
        ui->refreshPortsButton->setEnabled(true);

        continuousTimer->stop();
        stopVersionTimeout();
        stopCalibrateTimeout();
        
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
        if (!acceptingStream && !singleShotPending) return;

        QVector<float> armData;
        if (SerialProtocol::parseArmData(frame.data, armData) && armData.size() == 14) {
            processArmData(armData);
            updateUIWithArmData();

            if (singleShotPending) {
                singleShotPending = false;
                acceptingStream = false;
                logMessage("单次获取完成（收到一帧推送数据）");
            }
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
    case SerialProtocol::CMD_ENABLE_DATA_STREAM:
        if (result == SerialProtocol::RESULT_SUCCESS) streamEnabled = true;
        okFailText("启用遥操臂数据推送成功", "启用遥操臂数据推送失败");
        break;
    case SerialProtocol::CMD_GET_VERSION:
        if (result == SerialProtocol::RESULT_SUCCESS && payload.size() >= 4) {
            stopVersionTimeout();
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

void MainWindow::onLeftSingleGetClicked()
{
    ensureStreamEnabled();
    singleShotPending = true;
    acceptingStream = true;
    logMessage("左臂：单次获取（等待一帧推送数据，收到后自动停止更新）");
}

void MainWindow::onLeftContinuousGetClicked()
{
    ensureStreamEnabled();
    acceptingStream = true;
    singleShotPending = false;

    const int interval = ui->sendIntervalSpinBox->value();
    // 协议为“启用后设备主动推送”，默认不轮询发送；如需轮询，取消下面注释
    // continuousTimer->start(interval);
    Q_UNUSED(interval);

    logMessage("左臂：开始持续获取（接收推送）");
    showStatusMessage("持续获取：已开启（接收推送）");
}

void MainWindow::onLeftStopClicked()
{
    continuousTimer->stop();
    acceptingStream = false;
    singleShotPending = false;
    logMessage("停止获取（不再更新界面，仍保持串口接收）");
    showStatusMessage("已停止获取");
}

void MainWindow::onRightSingleGetClicked()
{
    ensureStreamEnabled();
    singleShotPending = true;
    acceptingStream = true;
    logMessage("右臂：单次获取（等待一帧推送数据，收到后自动停止更新）");
}

void MainWindow::onRightContinuousGetClicked()
{
    ensureStreamEnabled();
    acceptingStream = true;
    singleShotPending = false;
    logMessage("右臂：开始持续获取（接收推送）");
    showStatusMessage("持续获取：已开启（接收推送）");
}

void MainWindow::onRightStopClicked()
{
    onLeftStopClicked();
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
}

void MainWindow::onTorqueSetClicked()
{
    quint8 id = static_cast<quint8>(ui->idComboBox->currentText().toInt());
    float speed = ui->speedSpinBox->value();
    float acceleration = ui->accelerationSpinBox->value();
    float torque = ui->torqueSpinBox->value();
    float position = ui->positionSpinBox->value();

    // 构建扭矩控制命令（协议未更新：先按占位格式打包，后续按文档替换）
    QByteArray cmd = SerialProtocol::buildTorqueControlCommand(id, speed, acceleration, torque, position);
    writeData(cmd);

    logMessage(QString("扭矩设置（占位协议）: ID=%1, 速度=%2, 加速度=%3, 扭矩=%4, 位置=%5")
                   .arg(id).arg(speed).arg(acceleration).arg(torque).arg(position));
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
    if (leftArmData.isEmpty() || rightArmData.isEmpty()) return;

    // 更新左臂数据显示
    QString leftText = "左臂关节角度:\n";
    for (int i = 0; i < leftArmData.size(); ++i) {
        leftText += QString("关节%1: %2°\n").arg(i).arg(leftArmData[i], 0, 'f', 2);
    }
    ui->leftArmTextEdit->setText(leftText);

    // 更新右臂数据显示
    QString rightText = "右臂关节角度:\n";
    for (int i = 0; i < rightArmData.size(); ++i) {
        rightText += QString("关节%1: %2°\n").arg(i).arg(rightArmData[i], 0, 'f', 2);
    }
    ui->rightArmTextEdit->setText(rightText);

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
    logMessage(direction + "数据: " + hexString);
}

QString MainWindow::getCurrentTimeString()
{
    return QDateTime::currentDateTime().toString("[hh:mm:ss.zzz]");
}

void MainWindow::showStatusMessage(const QString &message, int timeout)
{
    statusBar()->showMessage(message, timeout);
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
