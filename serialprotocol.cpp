#include "serialprotocol.h"
#include <QDebug>

QByteArray SerialProtocol::buildCommandFrame(CommandType cmdType, const QByteArray &data)
{
    QByteArray frame;

    // 帧头
    frame.append(FRAME_HEADER);

    // 命令类型
    frame.append(static_cast<char>(cmdType));

    // 数据长度
    int dataLen = data.size();
    frame.append(static_cast<char>(dataLen));

    // 数据内容
    if (dataLen > 0) {
        frame.append(data);
    }

    // 计算校验和
    char checksum = calculateChecksum(static_cast<quint8>(cmdType), static_cast<quint8>(dataLen), data);
    frame.append(checksum);

    // 帧尾
    frame.append(FRAME_TAIL);

    return frame;
}

char SerialProtocol::calculateChecksum(quint8 cmdType, quint8 dataLength, const QByteArray &data)
{
    int sum = static_cast<int>(cmdType) + static_cast<int>(dataLength);

    // 累加数据内容
    for (char byte : data) {
        sum += static_cast<unsigned char>(byte);
    }

    // 取补码
    char checksum = ~(static_cast<char>(sum & 0xFF)) + 1;
    return checksum;
}

std::optional<SerialProtocol::Frame> SerialProtocol::tryExtractFrame(QByteArray &buffer)
{
    // 最小帧长度：头(1)+类型(1)+长度(1)+校验(1)+尾(1)=5
    if (buffer.size() < 5) {
        return std::nullopt;
    }

    // 找到帧头
    int headerIdx = buffer.indexOf(FRAME_HEADER);
    if (headerIdx < 0) {
        buffer.clear();
        return std::nullopt;
    }
    if (headerIdx > 0) {
        buffer.remove(0, headerIdx);
    }

    if (buffer.size() < 5) {
        return std::nullopt;
    }

    const quint8 cmd = static_cast<quint8>(buffer.at(1));
    const quint8 len = static_cast<quint8>(buffer.at(2));
    const int totalLen = 5 + static_cast<int>(len);

    if (buffer.size() < totalLen) {
        return std::nullopt; // 拆包：数据还没收全
    }

    // 验证帧尾
    if (buffer.at(totalLen - 1) != FRAME_TAIL) {
        // 帧头可能是误匹配：丢掉当前头字节，继续寻找下一个帧头
        buffer.remove(0, 1);
        return std::nullopt;
    }

    Frame frame;
    frame.cmdType = cmd;
    frame.dataLength = len;
    frame.data = buffer.mid(3, len);
    frame.checksum = static_cast<quint8>(buffer.at(3 + len));

    // 从buffer移除该帧（无论是否校验通过，交由调用方决定如何处理）
    buffer.remove(0, totalLen);

    return frame;
}

bool SerialProtocol::validateFrame(const Frame &frame)
{
    // 长度限制（文档0x00-0x80）
    if (frame.dataLength > 0x80) return false;
    if (frame.data.size() != frame.dataLength) return false;

    const char calc = calculateChecksum(frame.cmdType, frame.dataLength, frame.data);
    return static_cast<quint8>(calc) == frame.checksum;
}

QByteArray SerialProtocol::buildCalibrateCommand()
{
    return buildCommandFrame(CMD_CALIBRATE);
}

QByteArray SerialProtocol::buildGetVersionCommand()
{
    return buildCommandFrame(CMD_GET_VERSION);
}

QByteArray SerialProtocol::buildEnableDataStreamCommand()
{
    return buildCommandFrame(CMD_ENABLE_DATA_STREAM);
}

QByteArray SerialProtocol::buildDisableDataStreamCommand()
{
    return buildCommandFrame(CMD_DISABLE_DATA_STREAM);
}

QByteArray SerialProtocol::buildGetArmDataCommand()
{
    return buildCommandFrame(CMD_GET_ARM_DATA);
}

QByteArray SerialProtocol::buildTorqueControlCommand(quint8 id, float speed, float acceleration, float torque, float position)
{
    QByteArray data;

    // ID (1字节)
    data.append(static_cast<char>(id));
    
    // Position (2字节, int16小端, 假设无缩放或需要根据协议确认缩放)
    qint16 posInt = static_cast<qint16>(position);
    data.append(static_cast<char>(posInt & 0xFF));
    data.append(static_cast<char>((posInt >> 8) & 0xFF));

    // Speed (2字节, int16小端)
    qint16 speedInt = static_cast<qint16>(speed);
    data.append(static_cast<char>(speedInt & 0xFF));
    data.append(static_cast<char>((speedInt >> 8) & 0xFF));

    // Acceleration (1字节, uint8)
    quint8 accInt = static_cast<quint8>(acceleration);
    data.append(static_cast<char>(accInt));

    // Torque (2字节, int16小端)
    qint16 torqueInt = static_cast<qint16>(torque);
    data.append(static_cast<char>(torqueInt & 0xFF));
    data.append(static_cast<char>((torqueInt >> 8) & 0xFF));

    return buildCommandFrame(CMD_TORQUE_CONTROL, data);
}

bool SerialProtocol::parseArmData(const QByteArray &data, QVector<float> &armData)
{
    if (data.size() < 56) { // 14个float * 4字节
        return false;
    }

    armData.clear();
    armData.reserve(14);

    for (int i = 0; i < 56; i += 4) {
        float value = bytesToFloat(data, i);
        armData.append(value);
    }

    return armData.size() == 14;
}

QByteArray SerialProtocol::floatToBytes(float value)
{
    QByteArray bytes;
    bytes.resize(4);

    // 小端序
    const unsigned char *p = reinterpret_cast<const unsigned char*>(&value);
    bytes[0] = p[0];
    bytes[1] = p[1];
    bytes[2] = p[2];
    bytes[3] = p[3];

    return bytes;
}

float SerialProtocol::bytesToFloat(const QByteArray &bytes, int offset)
{
    if (bytes.size() < offset + 4) {
        return 0.0f;
    }

    float value;
    unsigned char *p = reinterpret_cast<unsigned char*>(&value);

    // 小端序
    p[0] = static_cast<unsigned char>(bytes[offset]);
    p[1] = static_cast<unsigned char>(bytes[offset + 1]);
    p[2] = static_cast<unsigned char>(bytes[offset + 2]);
    p[3] = static_cast<unsigned char>(bytes[offset + 3]);

    return value;
}
