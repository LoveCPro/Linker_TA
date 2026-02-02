#include "canprotocol.h"
#include <QDebug>

// CANArmDataCache 实现
CANArmDataCache::CANArmDataCache() {
    leftPart1.clear();
    leftPart2.clear();
    rightPart1.clear();
    rightPart2.clear();
}

void CANArmDataCache::clear() {
    leftPart1.clear();
    leftPart2.clear();
    rightPart1.clear();
    rightPart2.clear();
}

void CANArmDataCache::addLeftPart1(const QVector<qint16> &data) {
    leftPart1 = data;
}

void CANArmDataCache::addLeftPart2(const QVector<qint16> &data) {
    leftPart2 = data;
}

void CANArmDataCache::addRightPart1(const QVector<qint16> &data) {
    rightPart1 = data;
}

void CANArmDataCache::addRightPart2(const QVector<qint16> &data) {
    rightPart2 = data;
}

bool CANArmDataCache::isLeftComplete() const {
    return leftPart1.size() == 4 && leftPart2.size() == 3;
}

bool CANArmDataCache::isRightComplete() const {
    return rightPart1.size() == 4 && rightPart2.size() == 3;
}

QVector<float> CANArmDataCache::getLeftArmData() const {
    if (!isLeftComplete()) {
        return QVector<float>();
    }

    QVector<float> result;
    result.reserve(7);

    // 组合两部分数据
    for (int i = 0; i < 4; ++i) {
        result.append(static_cast<float>(leftPart1[i]) / 10.0f);
    }
    for (int i = 0; i < 3; ++i) {
        result.append(static_cast<float>(leftPart2[i]) / 10.0f);
    }

    return result;
}

QVector<float> CANArmDataCache::getRightArmData() const {
    if (!isRightComplete()) {
        return QVector<float>();
    }

    QVector<float> result;
    result.reserve(7);

    // 组合两部分数据
    for (int i = 0; i < 4; ++i) {
        result.append(static_cast<float>(rightPart1[i]) / 10.0f);
    }
    for (int i = 0; i < 3; ++i) {
        result.append(static_cast<float>(rightPart2[i]) / 10.0f);
    }

    return result;
}

void CANArmDataCache::clearLeft() {
    leftPart1.clear();
    leftPart2.clear();
}

void CANArmDataCache::clearRight() {
    rightPart1.clear();
    rightPart2.clear();
}

// CANProtocolUtils 实现
namespace CANProtocolUtils {

QVector<qint16> parseCANDataToInt16(const QByteArray &data) {
    QVector<qint16> result;

    // 每两个字节转换为一个int16（大端序）
    for (int i = 0; i < data.size() - 1; i += 2) {
        qint16 value = bytesToInt16(data, i);
        result.append(value);
    }

    return result;
}

CANDataFrame buildRequestFrame(quint16 requestId) {
    QByteArray data;
    data.resize(CANProtocol::CAN_MAX_DATA_LENGTH);
    data.fill(0);

    return CANDataFrame(requestId, data);
}

CANDataFrame buildCalibrateFrame() {
    QByteArray data;
    data.resize(CANProtocol::CAN_MAX_DATA_LENGTH);
    data.fill(0);

    return CANDataFrame(CANProtocol::CAN_ID_CALIBRATE, data);
}

CANDataFrame buildGetVersionFrame() {
    QByteArray data;
    data.resize(CANProtocol::CAN_MAX_DATA_LENGTH);
    data.fill(0);

    return CANDataFrame(CANProtocol::CAN_ID_GET_VERSION, data);
}

qint16 bytesToInt16(const QByteArray &data, int offset) {
    if (data.size() < offset + 2) {
        return 0;
    }

    // 大端序解析
    quint8 highByte = static_cast<quint8>(data.at(offset));
    quint8 lowByte = static_cast<quint8>(data.at(offset + 1));

    // 组合为有符号16位整数
    qint16 value = static_cast<qint16>((highByte << 8) | lowByte);
    return value;
}

} // namespace CANProtocolUtils
