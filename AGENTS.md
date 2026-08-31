# Agent 备注

## 版本号规则

版本号格式为 `主版本号.次版本号.修订号`，对应 `package.xml` 的 `<version>`，每次录制时写入该 session 的 `session.yaml` 的 `version` 字段。

判定标准只看**读取/回放路径**能否处理旧版本录制的 session（`session.yaml` + `rosbag` + `video`），不要求新旧版本写出的文件格式一致：

- **修订号 +1**：只修 bug、优化性能，未新增/删除功能，session 文件 schema 不变。
- **次版本号 +1**：新增/删除了功能，但新版本仍能正常读取/回放旧版本录制的 session。
- **主版本号 +1**：功能变动导致新版本无法正常读取/回放旧版本录制的 session。

递增某一位时，其后的低位版本号归零（标准 semver 约定，例如 1.1.0 → 1.2.0，不是 1.1.1）。

修改版本号时，只需改 `package.xml` 的 `<version>`；`data_recorder_core` 通过 CMake 的 `ament_package_xml()` 在编译期读取它并注入 `DATA_RECORDER_VERSION` 宏，无需在代码里另外硬编码。
