# USB键盘端点添加指南

本文档说明如何在现有的CH32V20x USB键盘项目中添加一个新的键盘端点。

## 需要修改的文件列表

### 1. `USBLIB/CONFIG/usb_conf.h`
**作用：** 配置USB端点数量和内存地址

**修改内容：**
```c
// 修改端点总数
#define EP_NUM (5)  // 原来是4，改为5

// 添加新端点的传输缓冲区地址
#define ENDP4_TXADDR (ENDP3_TXADDR + 0x10)
```

### 2. `USBLIB/CONFIG/usb_desc.h`
**作用：** 定义USB描述符大小

**修改内容：**
```c
// 更新配置描述符总大小（每增加一个接口+端点增加25字节）
#define USBD_SIZE_CONFIG_DESC 109  // 原来是84
```

### 3. `USBLIB/CONFIG/usb_desc.c`
**作用：** USB描述符定义

**修改内容：**
```c
// 1. 修改接口数量
0x04,  // bNumInterfaces (原来是0x03)

// 2. 在配置描述符末尾添加新的接口和端点描述符
/* Interface Descriptor (Keyboard) */
0x09, // bLength
0x04, // bDescriptorType
0x03, // bInterfaceNumber (新接口号)
0x00, // bAlternateSetting
0x01, // bNumEndpoints
0x03, // bInterfaceClass
0x01, // bInterfaceSubClass
0x01, // bInterfaceProtocol: Keyboard
0x00, // iInterface

/* HID Descriptor (Keyboard) */
0x09,                                                           // bLength
0x21,                                                           // bDescriptorType
0x11, 0x01,                                                     // bcdHID
0x00,                                                           // bCountryCode
0x01,                                                           // bNumDescriptors
0x22,                                                           // bDescriptorType
USBD_SIZE_REPORT_DESC_KB & 0xFF, USBD_SIZE_REPORT_DESC_KB >> 8, // wDescriptorLength

/* Endpoint Descriptor (Keyboard) */
0x07,                                           // bLength
0x05,                                           // bDescriptorType
0x84,                                           // bEndpointAddress: IN Endpoint 4
0x03,                                           // bmAttributes
DEF_ENDP_SIZE_KB & 0xFF, DEF_ENDP_SIZE_KB >> 8, // wMaxPacketSize
0x01                                            // bInterval: 1mS
```

### 4. `USBLIB/CONFIG/usb_prop.c`
**作用：** USB属性和请求处理

**修改内容：**
```c
// 1. 添加端点忙状态变量声明
extern uint8_t USBD_Endp1_Busy, USBD_Endp2_Busy, USBD_Endp3_Busy, USBD_Endp4_Busy;

// 2. 扩展HID相关数组
volatile uint8_t HID_Idle_Value[4] = {0};      // 原来是[3]
volatile uint8_t HID_Protocol_Value[4] = {0};  // 原来是[3]

// 3. 扩展报告描述符数组
ONE_DESCRIPTOR Report_Descriptor[4] = {        // 原来是[3]
    {(uint8_t *)USBD_KeyRepDesc, USBD_SIZE_REPORT_DESC_KB},
    {(uint8_t *)USBD_KeyRepDesc, USBD_SIZE_REPORT_DESC_KB},
    {(uint8_t *)USBD_KeyRepDesc, USBD_SIZE_REPORT_DESC_KB},
    {(uint8_t *)USBD_KeyRepDesc, USBD_SIZE_REPORT_DESC_KB},  // 新增
};

// 4. 扩展HID描述符数组
ONE_DESCRIPTOR Hid_Descriptor[4] = {           // 原来是[3]
    {(uint8_t *)&USBD_ConfigDescriptor[18], 0x09},
    {(uint8_t *)&USBD_ConfigDescriptor[43], 0x09},
    {(uint8_t *)&USBD_ConfigDescriptor[68], 0x09},
    {(uint8_t *)&USBD_ConfigDescriptor[93], 0x09},  // 新增
};

// 5. 在USBD_Reset函数中初始化新端点
SetEPType(ENDP4, EP_INTERRUPT);
SetEPTxAddr(ENDP4, ENDP4_TXADDR);
SetEPTxStatus(ENDP4, EP_TX_NAK);
_ClearDTOG_TX(ENDP4);
_ClearDTOG_RX(ENDP4);

// 6. 初始化新端点忙状态
USBD_Endp4_Busy = 0;

// 7. 更新所有接口范围检查（将2改为3）
// - USBD_GetReportDescriptor: if (wIndex0 > 3)
// - USBD_GetHidDescriptor: if (wIndex0 > 3)  
// - USBD_Get_Interface_Setting: else if (Interface > 3)
// - USBD_NoData_Setup中的HID_SET_IDLE和HID_SET_PROTOCOL: if (wIndex0 > 3)
// - HID_Get_Idle: if (wIndex0 > 3)
// - HID_Get_Protocol: if (wIndex0 > 3)
```

### 5. `USBLIB/CONFIG/usb_endp.c`
**作用：** 端点数据传输处理

**修改内容：**
```c
// 1. 添加端点忙状态变量
uint8_t USBD_Endp1_Busy, USBD_Endp2_Busy, USBD_Endp3_Busy, USBD_Endp4_Busy;

// 2. 添加端点回调函数
void EP4_IN_Callback(void)
{
    USBD_Endp4_Busy = 0;
}

// 3. 在USBD_ENDPx_DataUp函数中添加新端点处理
else if (endp == ENDP4)
{
    if (USBD_Endp4_Busy)
    {
        return USB_ERROR;
    }
    USB_SIL_Write(EP4_IN, pbuf, len);
    USBD_Endp4_Busy = 1;
    SetEPTxStatus(ENDP4, EP_TX_VALID);
}
```

### 6. `USBLIB/CONFIG/usb_istr.c`
**作用：** USB中断服务程序

**注意：** 此文件通常不需要修改，因为EP4_IN_Callback已经在pEpInt_IN数组中定义。

## 添加步骤总结

1. **配置阶段** - 修改`usb_conf.h`和`usb_desc.h`中的基础配置
2. **描述符阶段** - 在`usb_desc.c`中添加新的接口和端点描述符
3. **处理逻辑阶段** - 在`usb_prop.c`中添加端点初始化和请求处理
4. **数据传输阶段** - 在`usb_endp.c`中添加端点回调和数据发送函数

## 重要注意事项

1. **地址分配：** 每个端点需要唯一的传输缓冲区地址
2. **接口编号：** 新接口的编号应该是连续的（0,1,2,3...）
3. **端点地址：** 新端点的地址应该是连续的（0x81,0x82,0x83,0x84...）
4. **描述符大小：** 配置描述符总大小需要相应增加
5. **数组大小：** 所有相关数组都需要扩展以支持新的接口数量

## 使用新端点

添加完成后，可以使用以下函数通过新端点发送数据：
```c
USBD_ENDPx_DataUp(ENDP4, data_buffer, data_length);
```

## 验证checklist

- [ ] 端点数量配置正确
- [ ] 内存地址分配无冲突  
- [ ] 接口和端点描述符添加完整
- [ ] 配置描述符大小更新
- [ ] 所有数组大小正确扩展
- [ ] 端点初始化代码添加
- [ ] 回调函数实现完整
- [ ] 接口范围检查更新