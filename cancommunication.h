#ifndef CANCOMMUNICATION_H
#define CANCOMMUNICATION_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <atomic>
#include "canprotocol.h"

// PCAN-Basic API 类型定义（简化版，避免依赖PCAN-Basic.h）
// 实际使用时需要包含PCAN-Basic.h头文件
#ifndef PCAN_NO_BASIC_HEADER
// 如果没有安装PCAN-Basic SDK，使用以下类型定义作为占位
typedef unsigned long TPCANHandle;
typedef unsigned short TPCANStatus;
typedef unsigned char TPCANMsgFD;
typedef unsigned long TPCANTimestampFD;

// PCAN错误代码
#define PCAN_ERROR_OK 0x00000
#define PCAN_ERROR_INITIALIZE 0x00001
#define PCAN_ERROR_BUSOFF 0x00014
#define PCAN_ERROR_BUSLIGHT 0x00013
#define PCAN_ERROR_BUSHEAVY 0x00012

// PCAN通道
#define PCAN_USBBUS1 0x51
#define PCAN_USBBUS2 0x52
#define PCAN_USBBUS3 0x53
#define PCAN_USBBUS4 0x54

// PCAN波特率
#define PCAN_BAUD_1M 0x0014
#endif

// 工作线程：处理CAN消息接收
class CANWorkerThread : public QThread {
    Q_OBJECT

public:
    explicit CANWorkerThread(TPCANHandle handle, unsigned int baudrate, QObject *parent = nullptr);
    ~CANWorkerThread();

    void stop();
    bool isConnected() const { return m_connected; }
    bool sendFrame(const CANDataFrame &frame);

signals:
    void frameReceived(const CANDataFrame &frame);
    void errorOccurred(const QString &error);
    void connectionChanged(bool connected);

protected:
    void run() override;

private:
    bool m_connected;
    std::atomic<bool> m_running;
    QMutex m_mutex;

    // PCAN句柄
    TPCANHandle m_handle;
    unsigned int m_baudrate;

    // PCAN API调用（需要在实现文件中动态加载或链接）
    TPCANStatus initializeCAN(TPCANHandle handle, unsigned int baudrate);
    TPCANStatus uninitializeCAN(TPCANHandle handle);
    TPCANStatus readCAN(TPCANHandle handle, QByteArray &data, quint16 &id);
    TPCANStatus writeCAN(TPCANHandle handle, quint16 id, const QByteArray &data);
};

// CAN通信管理类
class CANCommunication : public QObject {
    Q_OBJECT

public:
    enum ConnectionStatus {
        Disconnected,
        Connected,
        Error
    };

    enum ArmType {
        LeftArm,
        RightArm,
        BothArms
    };

    explicit CANCommunication(QObject *parent = nullptr);
    ~CANCommunication();

    // 连接管理
    bool connect(const QString &channel = "PCAN_USBBUS1", quint32 bitrate = 1000000);
    void disconnect();
    bool isConnected() const { return m_status == Connected; }
    ConnectionStatus status() const { return m_status; }

    // 数据发送
    bool sendRequest(ArmType arm);
    bool sendCalibrate();
    bool sendGetVersion();
    bool sendCustomMessage(quint16 id, const QByteArray &data);

    // 获取数据缓存（用于同步模式）
    const CANArmDataCache& dataCache() const { return m_dataCache; }

signals:
    void statusChanged(int status);
    void leftArmDataReceived(const QVector<float> &data);
    void rightArmDataReceived(const QVector<float> &data);
    void versionReceived(const QString &version);
    void calibrationResultReceived(bool success);
    void errorOccurred(const QString &error);
    void logMessage(const QString &message, const QString &type = "info");

public slots:
    void onFrameReceived(const CANDataFrame &frame);

private:
    ConnectionStatus m_status;
    CANWorkerThread *m_worker;
    CANArmDataCache m_dataCache;
    QMutex m_cacheMutex;

    // 通道名称转换
    TPCANHandle channelToHandle(const QString &channel);
    unsigned int bitrateToPCAN(quint32 bitrate);

    // 处理特定ID的数据帧
    void handleDataFrame(const CANDataFrame &frame);
};

#endif // CANCOMMUNICATION_H
