#ifndef CANPROTOCOL_H
#define CANPROTOCOL_H

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

// CAN协议常量
namespace CANProtocol {
    // CAN ID定义
    const quint16 CAN_ID_LEFT_ARM_REQUEST  = 0x02;   // 左臂请求ID
    const quint16 CAN_ID_RIGHT_ARM_REQUEST = 0x03;   // 右臂请求ID
    const quint16 CAN_ID_BOTH_ARMS_REQUEST = 0x04;   // 双臂请求ID
    const quint16 CAN_ID_CALIBRATE          = 0xC1;   // 标定ID
    const quint16 CAN_ID_GET_VERSION        = 0x64;   // 获取版本ID
    
    // 响应ID定义
    const quint16 CAN_ID_LEFT_PART1         = 0x65;   // 左臂数据前4个关节 (ID 0-3)
    const quint16 CAN_ID_LEFT_PART2         = 0x66;   // 左臂数据后3个关节 (ID 4-6)
    const quint16 CAN_ID_RIGHT_PART1        = 0x67;   // 右臂数据前4个关节 (ID 7-10)
    const quint16 CAN_ID_RIGHT_PART2        = 0x68;   // 右臂数据后3个关节 (ID 11-13)

    // CAN消息数据长度
    const int CAN_MAX_DATA_LENGTH = 8;

    // 关节数量
    const int JOINTS_PER_ARM = 7;
}

// CAN数据帧结构
struct CANDataFrame {
    quint16 id;
    QByteArray data;  // 最多8字节

    CANDataFrame() : id(0) {
        data.resize(CANProtocol::CAN_MAX_DATA_LENGTH);
        data.fill(0);
    }

    CANDataFrame(quint16 id, const QByteArray &data) : id(id), data(data) {}
};

// CAN臂数据缓存（用于组合分帧）
class CANArmDataCache {
public:
    CANArmDataCache();

    // 清空所有缓存
    void clear();

    // 添加左臂数据分片
    void addLeftPart1(const QVector<qint16> &data);  // ID 0x65: 4个int16
    void addLeftPart2(const QVector<qint16> &data);  // ID 0x66: 3个int16

    // 添加右臂数据分片
    void addRightPart1(const QVector<qint16> &data); // ID 0x67: 4个int16
    void addRightPart2(const QVector<qint16> &data); // ID 0x68: 3个int16

    // 检查数据是否完整
    bool isLeftComplete() const;
    bool isRightComplete() const;

    // 获取完整臂数据（转换为float，原始值除以10）
    QVector<float> getLeftArmData() const;
    QVector<float> getRightArmData() const;

    // 清空已组合的数据
    void clearLeft();
    void clearRight();

private:
    QVector<qint16> leftPart1;  // ID 0x65: 4个int16
    QVector<qint16> leftPart2;  // ID 0x66: 3个int16
    QVector<qint16> rightPart1; // ID 0x67: 4个int16
    QVector<qint16> rightPart2; // ID 0x68: 3个int16

    // 辅助函数：将int16数组转换为float数组（除以10）
    static QVector<float> convertToFloat(const QVector<qint16> &intData);
};

// CAN协议工具函数
namespace CANProtocolUtils {
    // 解析CAN消息数据为int16数组（大端序）
    QVector<qint16> parseCANDataToInt16(const QByteArray &data);

    // 构建请求CAN帧
    CANDataFrame buildRequestFrame(quint16 requestId);

    // 构建标定CAN帧
    CANDataFrame buildCalibrateFrame();

    // 构建获取版本CAN帧
    CANDataFrame buildGetVersionFrame();

    // 从CAN帧数据中解析int16（大端序）
    qint16 bytesToInt16(const QByteArray &data, int offset);
}

#endif // CANPROTOCOL_H
