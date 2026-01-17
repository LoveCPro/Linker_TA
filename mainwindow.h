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

// 前向声明
namespace Ui {
class MainWindow;
}



class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 串口相关
    void onConnectClicked();
    void onPortsRefreshed();
    void onSerialDataReceived();
    void onSerialErrorOccurred(QSerialPort::SerialPortError error);

    // 左臂控制
    void onLeftSingleGetClicked();
    void onLeftContinuousGetClicked();
    void onLeftStopClicked();

    // 右臂控制
    void onRightSingleGetClicked();
    void onRightContinuousGetClicked();
    void onRightStopClicked();

    // 其他命令
    void onCalibrateClicked();
    void onClearLogClicked();
    void onSendCustomMessageClicked();

    // 扭矩设置
    void onTorqueSetClicked();

    // 定时器
    void onContinuousTimer();
    void updateCharts();

private:
    Ui::MainWindow *ui;
    QSerialPort *serialPort;
    QTimer *continuousTimer;
    QTimer *chartUpdateTimer;
    QTimer *versionTimeoutTimer;
    QTimer *calibrateTimeoutTimer;

    QByteArray rxBuffer;
    bool streamEnabled = false;
    bool acceptingStream = false; // “持续获取”开关（停止后不再更新UI，但仍可继续读串口）
    bool singleShotPending = false; // “单次获取”开关：收到一次56字节数据后自动停止更新

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
    QString getCurrentTimeString();
    void showStatusMessage(const QString &message, int timeout = 5000);
    void startVersionTimeout();
    void stopVersionTimeout();
    void startCalibrateTimeout();
    void stopCalibrateTimeout();
};

#endif // MAINWINDOW_H
