# NVDA Controller Client

本目录保存 NVDA 2026.1.1 官方发布的 x64 Controller Client 动态库及其原始许可证。
游戏运行时仅动态加载同目录的 `nvdaControllerClient.dll`，没有修改该库。

- 来源：<https://download.nvaccess.org/releases/2026.1.1/nvda_2026.1.1_controllerClient.zip>
- 上游压缩包 SHA-256：`2A3A70F729BE0C48120C5F24A81653202827C40A7986E289FFE2275A2B45011F`
- 动态库 SHA-256：`2FE60CF00BE929AAE32E95C1E1507A20ADA4902C8FEC273B3CC2D3BF5472932A`
- 许可证文件 SHA-256：`D2EFC89FBB7533572A472D34E07964951810A3A4F8D3159ECC2DCAA1452C5C26`
- 许可证：GNU Lesser General Public License 2.1

仅随程序分发 `x64/nvdaControllerClient.dll` 和上游 `license.txt`；调试符号、
导入库、头文件和其他架构文件不进入产品。
