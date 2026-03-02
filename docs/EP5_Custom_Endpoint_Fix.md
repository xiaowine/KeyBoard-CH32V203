/********************* EP5 自定义端点说明文档 *********************
 * File Name          : EP5_Custom_Endpoint_Info.md
 * Author             : System
 * Date               : 2026/03/02
 * Description        : EP5自定义端点Windows识别说明
 *******************************************************************/

## 问题解决：EP5 现在是真正的自定义HID设备

### 🔧 修复内容

原问题：EP5端点在Windows中显示为"标准键盘"
**原因：** 接口4的HID描述符仍然引用键盘报告描述符(USBD_KeyRepDesc)

### ✅ 已完成修复

#### 1. **新增自定义HID报告描述符**
- 文件：`usb_desc.c`
- 新增：`USBD_CustomRepDesc[34]` - 专用于原始数据传输
- 特性：vendor-defined用法页(0xFF00)，支持32字节双向传输

#### 2. **更新HID描述符引用**  
- 接口4的HID描述符现在引用 `USBD_SIZE_REPORT_DESC_CUSTOM` 
- 报告描述符映射：接口4 → `USBD_CustomRepDesc` (而不是键盘报告描述符)

#### 3. **报告描述符详情**
```c
// 自定义HID报告描述符 - 32字节双向原始数据
0x06, 0x00, 0xFF,    // Usage Page (Vendor Defined 0xFF00)  
0x09, 0x01,          // Usage (0x01)
0xA1, 0x01,          // Collection (Application)
0x09, 0x01,          // Usage (0x01) 
// INPUT: 32字节从设备到主机
0x15, 0x00,          // Logical Minimum (0)
0x25, 0xFF,          // Logical Maximum (255)
0x95, 0x20,          // Report Count (32)
0x75, 0x08,          // Report Size (8)  
0x81, 0x02,          // Input (Data,Variable,Absolute)
// OUTPUT: 32字节从主机到设备
0x09, 0x01,          // Usage (0x01)
0x95, 0x20,          // Report Count (32) 
0x75, 0x08,          // Report Size (8)
0x91, 0x02,          // Output (Data,Variable,Absolute)
0xC0                 // End Collection
```

### 🖥️ Windows设备管理器中的识别

**修复前：** 
- 设备名称：标准键盘
- 类别：键盘

**修复后：**
- 设备名称：HID兼容设备 或 USB输入设备  
- 类别：人体学输入设备 (Human Interface Devices)
- VID/PID：显示你的设备ID 
- 接口：显示为自定义HID接口

### 💻 编程接口

EP5现在支持完全的原始数据传输：

```c
// 发送32字节原始数据到PC
uint8_t raw_data[32] = {0x01, 0x02, 0x03, /*...*/};
USBD_SendCustomData(raw_data, 32);

// 接收来自PC的32字节数据  
uint8_t received_data[32];
uint16_t len = USBD_GetCustomData(received_data, 32);
```

### 🔍 与PC软件通信

现在可以使用标准HID API与设备通信：

**Python示例：**
```python
import hid

# 找到你的自定义设备
device = hid.device()
device.open(vendor_id, product_id)  # 你的VID/PID

# 发送32字节数据到设备
data_to_send = [0x00] + [0x01, 0x02, 0x03] + [0] * 28  # Report ID + 数据
device.write(data_to_send)

# 接收32字节数据  
received_data = device.read(32)
```

**C# 示例：**
```csharp
using HidLibrary;

var device = HidDevices.Enumerate(vendorId, productId).FirstOrDefault();
if (device != null)
{
    device.OpenDevice();
    
    // 发送数据
    byte[] dataToSend = new byte[32];
    dataToSend[0] = 0x01; // 数据内容
    device.Write(dataToSend);
    
    // 接收数据
    var report = device.Read();
    byte[] receivedData = report.Data;
}
```

### ⚠️ 重要说明

1. **设备重新枚举**：修改后需要重新插拔USB或重启设备
2. **驱动安装**：Windows可能需要安装通用HID驱动  
3. **权限**：某些系统可能需要管理员权限访问自定义HID设备
4. **数据格式**：现在支持完全原始的32字节数据，无需HID键盘格式限制

### 🎯 验证方法

1. **设备管理器**：检查设备是否显示为"HID兼容设备"
2. **USBTreeView等工具**：查看HID报告描述符是否为自定义格式
3. **编程测试**：使用HID库连接并测试数据收发

现在EP5是真正的自定义HID端点，可以传输任意32字节数据！