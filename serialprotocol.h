#ifndef SERIALPROTOCOL_H
#define SERIALPROTOCOL_H

#include <QByteArray>
#include <QVector>
#include <optional>

class SerialProtocol
{
public:
    // 命令类型枚举
    enum CommandType {
        CMD_GET_ARM_DATA = 0x01,      // 获取臂数据
        CMD_GET_VERSION = 0x14,       // 读取版本号
        CMD_ENABLE_DATA_STREAM = 0x15, // 启用数据流
        CMD_CALIBRATE = 0x17,         // 标定
        // 注意：扭矩设置协议文档未更新，这里先预留命令号（后续按协议改）
        CMD_TORQUE_CONTROL = 0x20,    // 扭矩控制（占位）
        CMD_SET_PARAMS = 0x21         // 参数设置（占位）
    };

    // 结果码
    enum ResultCode {
        RESULT_SUCCESS = 0x00,
        RESULT_FAIL = 0x01,
        RESULT_UNKNOWN_CMD = 0xFD,
        RESULT_CHECKSUM_ERROR = 0xFF
    };

    // 帧结构
    static const char FRAME_HEADER = 0xAA;
    static const char FRAME_TAIL = 0x55;

    // 构建命令帧
    static QByteArray buildCommandFrame(CommandType cmdType, const QByteArray &data = QByteArray());

    struct Frame {
        quint8 cmdType = 0;
        quint8 dataLength = 0;
        QByteArray data;     // 仅数据区（不含checksum/尾）
        quint8 checksum = 0; // 原始checksum字节
    };

    // 从“可能包含粘包/拆包”的buffer里尝试取出一帧；成功时会从buffer中移除该帧。
    // 返回：std::nullopt 表示当前buffer不足以组成完整帧或找不到有效帧头。
    static std::optional<Frame> tryExtractFrame(QByteArray &buffer);

    // 校验一帧（头/尾/长度/校验和）
    static bool validateFrame(const Frame &frame);

    // 计算校验和
    static char calculateChecksum(quint8 cmdType, quint8 dataLength, const QByteArray &data);

    // 特定命令构建
    static QByteArray buildCalibrateCommand();
    static QByteArray buildGetVersionCommand();
    static QByteArray buildEnableDataStreamCommand();
    static QByteArray buildGetArmDataCommand();

    // 扭矩控制命令（待协议更新）
    static QByteArray buildTorqueControlCommand(quint8 id, float speed, float acceleration, float torque, float position);
    static QByteArray buildSetParamsCommand(quint8 id, float speed, float acceleration, float torque, float target);

    // 解析臂数据
    static bool parseArmData(const QByteArray &data, QVector<float> &armData);

    // 浮点数转字节数组（小端序）
    static QByteArray floatToBytes(float value);
    static float bytesToFloat(const QByteArray &bytes, int offset = 0);
};

#endif // SERIALPROTOCOL_H
