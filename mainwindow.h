#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QTimer>

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>

#include "serialprotocol.h"

#define APP_VERSION "1.0.0"

// 前向声明
class CANCommunication;
class QPushButton;
class QLabel;
class QSpinBox;

namespace Ui {
class MainWindow;
}



class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 通信模式枚举
    enum class CommunicationMode {
        Serial,  // 无线摇操臂（串口）
        CAN      // 有线摇操臂（CAN总线）
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 串口相关
    void onConnectClicked();
    void onPortsRefreshed();
    void onSerialDataReceived();
    void onSerialErrorOccurred(QSerialPort::SerialPortError error);

    // 臂控制
    void onArmGetClicked();

    // 其他命令
    void onCalibrateClicked();
    void onClearLogClicked();
    void onSendCustomMessageClicked();

    // 扭矩设置
    void onTorqueSetClicked();

    // 定时器
    void onContinuousTimer();
    void updateCharts();

    // 通信模式相关
    void onCommunicationModeChanged(int index);

    // CAN相关
    void onCANConnectClicked();
    void onCANLeftArmSingleClicked();
    void onCANLeftArmContinuousClicked();
    void onCANRightArmSingleClicked();
    void onCANRightArmContinuousClicked();
    void onCANLeftArmPollTimeout();
    void onCANRightArmPollTimeout();
    
    // 双臂操作槽函数
    void onCANBothArmsSingleClicked();
    void onCANBothArmsContinuousClicked();
    void onCANBothArmsPollTimeout();

    // CAN相关事件
    void onCANStatusChanged(int status);
    void onCANLeftArmDataReceived(const QVector<float> &data);
    void onCANRightArmDataReceived(const QVector<float> &data);
    void onCANLogMessage(const QString &message, const QString &type);
    void onCANErrorOccurred(const QString &error);

private:
    Ui::MainWindow *ui;
    QSerialPort *serialPort;
    QTimer *continuousTimer;
    QTimer *chartUpdateTimer;
    QTimer *armUpdateTimer;
    QTimer *versionTimeoutTimer;
    QTimer *versionRetryTimer;
    QTimer *calibrateTimeoutTimer;

    // CAN通信
    CANCommunication *canComm;
    CommunicationMode currentMode;
    QTimer *leftArmPollTimer;
    QTimer *rightArmPollTimer;
    QTimer *bothArmsPollTimer;
    bool leftArmContinuousEnabled;
    bool rightArmContinuousEnabled;
    bool bothArmsContinuousEnabled;

    // 频率统计
    qint64 leftArmStartTime = 0;
    int leftArmFrameCount = 0;
    qint64 rightArmStartTime = 0;
    int rightArmFrameCount = 0;
    qint64 bothArmsStartTime = 0;
    int bothArmsFrameCount = 0;
    qint64 leftSendStartTime = 0;
    int leftSendCount = 0;
    qint64 rightSendStartTime = 0;
    int rightSendCount = 0;
    qint64 bothSendStartTime = 0;
    int bothSendCount = 0;
    
    // 无线模式频率统计
    qint64 serialStartTime = 0;
    int serialRxCount = 0;
    
    // 动态添加的按钮
    QPushButton *canBothArmsSingleButton = nullptr;
    QPushButton *canBothArmsContinuousButton = nullptr;

    QByteArray rxBuffer;
    bool streamEnabled = false;
    bool acceptingStream = false; // 臂数据获取开关（停止后不再更新UI，但仍可继续读串口）
    int versionRequestCount = 0;
    bool versionReceived = false;
    bool calibrating = false; // 校准状态标志，true表示正在等待校准响应

    // 数据存储
    QVector<float> leftArmData;
    QVector<float> rightArmData;
    QVector<QVector<float>> leftArmHistory;
    QVector<QVector<float>> rightArmHistory;

    // 图表
    QChart *leftArmChart;
    QChart *rightArmChart;
    QVector<QLineSeries*> leftSeries;
    QVector<QLineSeries*> rightSeries;
    // 左右臂分别使用独立的坐标轴，避免一个 axis 被两个 QChart 拥有导致段错误
    QDateTimeAxis *leftAxisX;
    QValueAxis *leftAxisY;
    QDateTimeAxis *rightAxisX;
    QValueAxis *rightAxisY;

    // 初始化函数
    void initUI();
    void initCharts();
    void initConnections();

    // 串口操作
    void openSerialPort();
    void closeSerialPort();
    void writeData(const QByteArray &data);

    // 数据解析
    void processArmData(const QVector<float> &data);
    void updateUIWithArmData();
    void handleProtocolFrame(const SerialProtocol::Frame &frame);
    void ensureStreamEnabled();

    // 日志记录
    void logMessage(const QString &message);
    void logHexData(const QByteArray &data, bool isSend = false);

    // 工具函数
    void setOperationButtonsEnabled(bool enabled);
    void sendVersionRequest();
    QString getCurrentTimeString();
    void showStatusMessage(const QString &message, int timeout = 5000);
    void startVersionTimeout();
    void stopVersionTimeout();
    void startCalibrateTimeout();
    void stopCalibrateTimeout();
    void updateCalibrateButtonState(); // 更新校准按钮状态
    QString parseVersionNumber(const QByteArray &versionBytes); // 解析版本号

    // CAN相关辅助函数
    void initCANCommunication();
    void cleanupCANCommunication();
    void updateUIForCommunicationMode(CommunicationMode mode);
    void enableSerialControls(bool enabled);
    void enableCANControls(bool enabled);
    void stopCANPolling();
    void clearArmDataUI();
};

#endif // MAINWINDOW_H
