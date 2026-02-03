#include "cancommunication.h"
#include <QDebug>
#include <QMutexLocker>
#include <windows.h>

// PCAN-Basic 数据结构
#pragma pack(push, 1)
typedef struct {
    quint32 ID;
    quint8  MSGTYPE;
    quint8  LEN;
    quint8  DATA[8];
} TPCANMsg;

typedef struct {
    quint32 millis;
    quint16 millis_overflow;
    quint16 micros;
} TPCANTimestamp;
#pragma pack(pop)

// PCAN-Basic API 函数指针类型
typedef TPCANStatus (__stdcall *FP_CAN_Initialize)(TPCANHandle, TPCANStatus);
typedef TPCANStatus (__stdcall *FP_CAN_Uninitialize)(TPCANHandle);
typedef TPCANStatus (__stdcall *FP_CAN_Read)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
typedef TPCANStatus (__stdcall *FP_CAN_Write)(TPCANHandle, TPCANMsg*);

// 全局函数指针（动态加载）
static HMODULE s_pcanDll = nullptr;
static FP_CAN_Initialize s_canInitialize = nullptr;
static FP_CAN_Uninitialize s_canUninitialize = nullptr;
static FP_CAN_Read s_canRead = nullptr;
static FP_CAN_Write s_canWrite = nullptr;

// 动态加载PCAN-Basic DLL
static bool loadPCANLibrary() {
    if (s_pcanDll != nullptr) {
        return true;  // 已加载
    }

    // 尝试加载PCAN-Basic DLL
    s_pcanDll = LoadLibraryA("PCANBasic.dll");

    if (s_pcanDll == nullptr) {
        qWarning() << "Failed to load PCANBasic.dll. Please install PCAN-Basic driver.";
        return false;
    }

    // 获取函数地址
    s_canInitialize = (FP_CAN_Initialize)GetProcAddress(s_pcanDll, "CAN_Initialize");
    s_canUninitialize = (FP_CAN_Uninitialize)GetProcAddress(s_pcanDll, "CAN_Uninitialize");
    s_canRead = (FP_CAN_Read)GetProcAddress(s_pcanDll, "CAN_Read");
    s_canWrite = (FP_CAN_Write)GetProcAddress(s_pcanDll, "CAN_Write");

    if (!s_canInitialize || !s_canUninitialize || !s_canRead || !s_canWrite) {
        qWarning() << "Failed to get PCAN-Basic function addresses.";
        FreeLibrary(s_pcanDll);
        s_pcanDll = nullptr;
        return false;
    }

    return true;
}

// CANWorkerThread 实现
CANWorkerThread::CANWorkerThread(TPCANHandle handle, unsigned int baudrate, QObject *parent)
    : QThread(parent)
    , m_connected(false)
    , m_running(false)
    , m_handle(handle)
    , m_baudrate(baudrate)
{
}

CANWorkerThread::~CANWorkerThread() {
    stop();
}

void CANWorkerThread::stop() {
    if (m_running) {
        m_running = false;
        wait();

        if (m_connected) {
            uninitializeCAN(m_handle);
            m_connected = false;
        }
    }
}

void CANWorkerThread::run() {
    m_running = true;

    // 初始化PCAN
    TPCANStatus status = initializeCAN(m_handle, m_baudrate);

    if (status != PCAN_ERROR_OK) {
        emit errorOccurred(QString("PCAN初始化失败 (错误码: 0x%1)").arg(status, 4, 16, QChar('0')));
        emit connectionChanged(false);
        m_running = false;
        return;
    }

    m_connected = true;
    emit connectionChanged(true);

    qDebug() << "PCAN initialized successfully";

    // 接收循环
    QByteArray data;
    quint16 id;

    while (m_running) {
        data.resize(8);
        status = readCAN(m_handle, data, id);

        if (status == PCAN_ERROR_OK) {
            // 成功接收到数据
            CANDataFrame frame(id, data);
            emit frameReceived(frame);
        } else if (status == 0x20) { // PCAN_ERROR_QRCVEMPTY
            // 没有数据，继续等待
            msleep(1);
        } else {
            // 其他错误
            emit errorOccurred(QString("PCAN读取错误 (错误码: 0x%1)").arg(status, 4, 16, QChar('0')));
            msleep(10);
        }
    }

    // 清理
    if (m_connected) {
        uninitializeCAN(m_handle);
        m_connected = false;
    }
    emit connectionChanged(false);
}

bool CANWorkerThread::sendFrame(const CANDataFrame &frame) {
    if (!m_connected) return false;
    return writeCAN(m_handle, frame.id, frame.data) == PCAN_ERROR_OK;
}

TPCANStatus CANWorkerThread::initializeCAN(TPCANHandle handle, unsigned int baudrate) {
    if (!loadPCANLibrary()) {
        return PCAN_ERROR_INITIALIZE;
    }

    if (s_canInitialize) {
        return s_canInitialize(handle, static_cast<TPCANStatus>(baudrate));
    }
    return PCAN_ERROR_INITIALIZE;
}

TPCANStatus CANWorkerThread::uninitializeCAN(TPCANHandle handle) {
    if (s_canUninitialize) {
        return s_canUninitialize(handle);
    }
    return PCAN_ERROR_OK;
}

TPCANStatus CANWorkerThread::readCAN(TPCANHandle handle, QByteArray &data, quint16 &id) {
    if (!s_canRead) {
        return 0x20; // PCAN_ERROR_QRCVEMPTY
    }

    TPCANMsg canMsg;
    TPCANTimestamp timestamp;

    TPCANStatus status = s_canRead(handle, &canMsg, &timestamp);

    if (status == PCAN_ERROR_OK) {
        id = static_cast<quint16>(canMsg.ID);
        data = QByteArray(reinterpret_cast<char*>(canMsg.DATA), canMsg.LEN);
    }

    return status;
}

TPCANStatus CANWorkerThread::writeCAN(TPCANHandle handle, quint16 id, const QByteArray &data) {
    if (!s_canWrite) {
        return PCAN_ERROR_INITIALIZE;
    }

    TPCANMsg canMsg;
    canMsg.ID = id;
    canMsg.MSGTYPE = 0x00; // PCAN_MESSAGE_STANDARD
    canMsg.LEN = static_cast<quint8>(qMin(data.size(), 8));
    memset(canMsg.DATA, 0, 8);
    memcpy(canMsg.DATA, data.constData(), canMsg.LEN);

    return s_canWrite(handle, &canMsg);
}


// CANCommunication 实现
CANCommunication::CANCommunication(QObject *parent)
    : QObject(parent)
    , m_status(Disconnected)
    , m_worker(nullptr)
{
}

CANCommunication::~CANCommunication() {
    disconnect();
}

bool CANCommunication::connect(const QString &channel, quint32 bitrate) {
    if (m_status == Connected) {
        emit logMessage("CAN已经连接", "warning");
        return true;
    }

    // 检查PCAN库
    if (!loadPCANLibrary()) {
        QString error = "无法加载PCANBasic.dll。请安装PCAN-Basic驱动程序。";
        emit errorOccurred(error);
        emit logMessage(error, "error");
        return false;
    }

    // 创建工作线程
    TPCANHandle handle = channelToHandle(channel);
    unsigned int pcanBaudrate = bitrateToPCAN(bitrate);
    m_worker = new CANWorkerThread(handle, pcanBaudrate, this);
    m_worker->start();

    // 连接信号
    QObject::connect(m_worker, &CANWorkerThread::frameReceived, this, &CANCommunication::onFrameReceived);
    QObject::connect(m_worker, &CANWorkerThread::errorOccurred, this, &CANCommunication::errorOccurred);
    QObject::connect(m_worker, &CANWorkerThread::connectionChanged, [this](bool connected) {
        if (connected) {
            m_status = Connected;
            emit statusChanged(static_cast<int>(Connected));
            emit logMessage("CAN连接成功", "success");
        } else {
            m_status = Disconnected;
            emit statusChanged(static_cast<int>(Disconnected));
        }
    });

    // 等待连接结果
    QTimer::singleShot(1000, [this]() {
        if (m_worker && !m_worker->isConnected()) {
            emit errorOccurred("CAN连接超时");
            emit logMessage("CAN连接超时", "error");
        }
    });

    return true;
}

void CANCommunication::disconnect() {
    // 确保所有定时器和轮询都已停止（在 MainWindow 层级应该已经处理，但这里做双重保险）
    
    if (m_worker) {
        emit logMessage("正在断开CAN连接...", "info");
        m_worker->stop();
        // 缩短等待时间，避免阻塞 UI 太久，如果线程还没退出就强制继续
        if (!m_worker->wait(1000)) {
            emit logMessage("CAN工作线程停止超时", "warning");
        }
        delete m_worker;
        m_worker = nullptr;
    }

    if (m_status == Connected) {
        m_status = Disconnected;
        m_dataCache.clear();
        emit statusChanged(static_cast<int>(Disconnected));
        emit logMessage("CAN已断开", "info");
    }
}

bool CANCommunication::sendRequest(ArmType arm) {
    if (!isConnected()) {
        emit logMessage("CAN未连接，无法发送请求", "error");
        return false;
    }

    quint16 requestId;
    if (arm == LeftArm) {
        requestId = CANProtocol::CAN_ID_LEFT_ARM_REQUEST;
    } else if (arm == RightArm) {
        requestId = CANProtocol::CAN_ID_RIGHT_ARM_REQUEST;
    } else {
        requestId = CANProtocol::CAN_ID_BOTH_ARMS_REQUEST;
    }

    CANDataFrame frame = CANProtocolUtils::buildRequestFrame(requestId);

    // 清空对应臂的缓存
    QMutexLocker locker(&m_cacheMutex);
    if (arm == LeftArm) {
        m_dataCache.clearLeft();
    } else if (arm == RightArm) {
        m_dataCache.clearRight();
    } else {
        m_dataCache.clear();
    }
    locker.unlock();

    // 发送请求
    QString armName;
    if (arm == LeftArm) armName = "左臂";
    else if (arm == RightArm) armName = "右臂";
    else armName = "双臂";

    emit logMessage(QString("发送%1位置请求 (ID=0x%2)")
                   .arg(armName)
                   .arg(requestId, 2, 16, QChar('0')), "info");

    return m_worker->sendFrame(frame);
}

bool CANCommunication::sendCalibrate() {
    if (!isConnected()) {
        emit logMessage("CAN未连接，无法发送标定命令", "error");
        return false;
    }

    CANDataFrame frame = CANProtocolUtils::buildCalibrateFrame();

    emit logMessage(QString("发送标定命令 (ID=0x%1)")
                   .arg(CANProtocol::CAN_ID_CALIBRATE, 2, 16, QChar('0')), "info");

    return m_worker->sendFrame(frame);
}

bool CANCommunication::sendGetVersion() {
    if (!isConnected()) {
        emit logMessage("CAN未连接，无法发送获取版本命令", "error");
        return false;
    }

    CANDataFrame frame = CANProtocolUtils::buildGetVersionFrame();

    emit logMessage(QString("发送获取版本命令 (ID=0x%1)")
                   .arg(CANProtocol::CAN_ID_GET_VERSION, 2, 16, QChar('0')), "info");

    return m_worker->sendFrame(frame);
}

bool CANCommunication::sendCustomMessage(quint16 id, const QByteArray &data) {
    if (!isConnected()) {
        emit logMessage("CAN未连接，无法发送自定义消息", "error");
        return false;
    }

    emit logMessage(QString("发送自定义消息 (ID=0x%1, 数据=%2)")
                   .arg(id, 2, 16, QChar('0'))
                   .arg(QString(data.toHex(' '))), "info");

    return m_worker->sendFrame(CANDataFrame(id, data));
}

void CANCommunication::onFrameReceived(const CANDataFrame &frame) {
    QMutexLocker locker(&m_cacheMutex);

    // 根据CAN ID处理不同类型的数据
    switch (frame.id) {
    case CANProtocol::CAN_ID_GET_VERSION: {
        emit logMessage(QString("接收到版本信息 (ID=0x%1) Data=%2")
                       .arg(frame.id, 2, 16, QChar('0'))
                       .arg(QString(frame.data.toHex())), "response");

        // 版本号格式: [硬件版本] [软件版本] [保留1] [保留2]
        // 示例: 72 64 01 00 -> 硬件版本: V1.1.4, 软件版本: V1.0.0
        if (frame.data.size() >= 2) {
            quint8 hwVersion = static_cast<quint8>(frame.data.at(0));
            quint8 swVersion = static_cast<quint8>(frame.data.at(1));

            // 将数字转换为X.Y.Z格式
            auto formatVersion = [](quint8 v) -> QString {
                int major = v / 100;
                int minor = (v % 100) / 10;
                int patch = v % 10;
                return QString("V%1.%2.%3").arg(major).arg(minor).arg(patch);
            };

            QString versionStr = QString("硬件版本: %1, 软件版本: %2")
                                    .arg(formatVersion(hwVersion))
                                    .arg(formatVersion(swVersion));
            emit versionReceived(versionStr);
        }
        break;
    }

    case CANProtocol::CAN_ID_CALIBRATE: {
        emit logMessage(QString("接收到标定响应 (ID=0x%1) Data=%2")
                       .arg(frame.id, 2, 16, QChar('0'))
                       .arg(QString(frame.data.toHex())), "response");

        if (frame.data.size() > 0) {
            quint8 result = static_cast<quint8>(frame.data.at(0));
            bool success = (result == 1);
            emit calibrationResultReceived(success);
            
            if (success) {
                emit logMessage("CAN标定成功", "success");
            } else {
                emit logMessage("CAN标定失败", "error");
            }
        }
        break;
    }
    
    // 左臂分片1 (ID 0-3)
    case CANProtocol::CAN_ID_LEFT_PART1: {
        QVector<qint16> data = CANProtocolUtils::parseCANDataToInt16(frame.data);
        // Data length: 8 bytes -> 4 int16s
        if (data.size() >= 4) {
            m_dataCache.addLeftPart1(data.mid(0, 4));
            // logMessage removed to avoid flooding log during high freq polling
            // emit logMessage("接收到左臂数据分片1 (ID=0x65)", "response");

            // 检查是否完整
            if (m_dataCache.isLeftComplete()) {
                QVector<float> armData = m_dataCache.getLeftArmData();
                emit leftArmDataReceived(armData);
                m_dataCache.clearLeft();
            }
        }
        break;
    }

    // 左臂分片2 (ID 4-6)
    case CANProtocol::CAN_ID_LEFT_PART2: {
        QVector<qint16> data = CANProtocolUtils::parseCANDataToInt16(frame.data);
        // Data length: 6 bytes -> 3 int16s
        if (data.size() >= 3) {
            m_dataCache.addLeftPart2(data.mid(0, 3));
            
            if (m_dataCache.isLeftComplete()) {
                QVector<float> armData = m_dataCache.getLeftArmData();
                emit leftArmDataReceived(armData);
                m_dataCache.clearLeft();
            }
        }
        break;
    }

    // 右臂分片1 (ID 7-10)
    case CANProtocol::CAN_ID_RIGHT_PART1: {
        QVector<qint16> data = CANProtocolUtils::parseCANDataToInt16(frame.data);
        // Data length: 8 bytes -> 4 int16s
        if (data.size() >= 4) {
            m_dataCache.addRightPart1(data.mid(0, 4));

            if (m_dataCache.isRightComplete()) {
                QVector<float> armData = m_dataCache.getRightArmData();
                emit rightArmDataReceived(armData);
                m_dataCache.clearRight();
            }
        }
        break;
    }

    // 右臂分片2 (ID 11-13)
    case CANProtocol::CAN_ID_RIGHT_PART2: {
        QVector<qint16> data = CANProtocolUtils::parseCANDataToInt16(frame.data);
        // Data length: 6 bytes -> 3 int16s
        if (data.size() >= 3) {
            m_dataCache.addRightPart2(data.mid(0, 3));

            if (m_dataCache.isRightComplete()) {
                QVector<float> armData = m_dataCache.getRightArmData();
                emit rightArmDataReceived(armData);
                m_dataCache.clearRight();
            }
        }
        break;
    }

    default:
        // 其他CAN ID不处理
        break;
    }
}

void CANCommunication::handleDataFrame(const CANDataFrame &frame) {
    // 此函数保留用于扩展
    Q_UNUSED(frame)
}

TPCANHandle CANCommunication::channelToHandle(const QString &channel) {
    if (channel == "PCAN_USBBUS1") return PCAN_USBBUS1;
    if (channel == "PCAN_USBBUS2") return PCAN_USBBUS2;
    if (channel == "PCAN_USBBUS3") return PCAN_USBBUS3;
    if (channel == "PCAN_USBBUS4") return PCAN_USBBUS4;
    return PCAN_USBBUS1; // 默认
}

unsigned int CANCommunication::bitrateToPCAN(quint32 bitrate) {
    if (bitrate == 1000000) return PCAN_BAUD_1M;
    // 可以添加其他波特率支持
    return PCAN_BAUD_1M;
}
