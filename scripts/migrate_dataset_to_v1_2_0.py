#!/usr/bin/env python3
"""将旧版本录制数据集升级为 1.2.0 版本格式。

旧格式（`session.yaml` 无 `version` 字段）与 1.2.0 格式的差异：
  - `session.yaml` 顶层新增 `version: 1.2.0`；`backend: video` 的话题条目
    新增 `offered_qos_profiles`（真实 QoS 已不可回溯，用固定占位值）。
  - video 话题的 CSV 从 3 列 (`frame_index,ros_stamp_ns,pts_ns`) 扩展为 7 列
    (`frame_index,recv_stamp_ns,header_stamp_ns,pts_ns,frame_id,encoding,is_bigendian`)：
      * `header_stamp_ns` 就是旧的 `ros_stamp_ns`（语义相同，只是改名）。
      * `recv_stamp_ns`（节点收到图像消息的真实时刻）无法从旧数据精确还原，
        按 header 时间戳在同一 session 的 rosbag 里找同一相机的
        `color/camera_info` / `color/metadata` 消息做估计：两者都匹配到时取
        接收时刻均值；只匹配到一个用那一个；都匹配不到（丢帧）时退化为
        `header_stamp_ns + session 内 camera_info 的 (recv-header) 中位数偏移`，
        并打印警告。
      * `frame_id` 同样从匹配到的 `camera_info`/`metadata` 消息里取
        `header.frame_id`；两者都匹配到但不一致时打印警告，优先取非空值，
        都非空时优先 metadata；都没匹配到时留空。
      * `encoding`/`is_bigendian` 用固定值 `rgb8`/`0`（已核实本仓库所有
        `color/image_raw` 均为此编码）。
  - rosbag（`rosbag/metadata.yaml` + `.db3`）原样拷贝，不做存储后端转码。

转换后的 `session.yaml` 会新增 `migrated_from` 字段记录迁移来源，
并通过 YAML 注释说明时间戳估算方法和固定值的含义。
默认跳过已有 version 字段的会话；使用 --copy-skipped 可将这些会话及
会话目录之外的附属文件原样复制到输出目录（输出目录必须尚不存在）。

用法:
    source /opt/ros/humble/setup.bash   # 提供 rosbag2_py / rclpy
    python3 src/ROS2-recorder/scripts/migrate_dataset_to_v1_2_0.py \
        --input recordings/gripper_pick_4 \
        --output recordings/gripper_pick_4_v1_2_0
"""

from __future__ import annotations

import argparse
import csv
import shutil
import statistics
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

try:
    import rosbag2_py
    import yaml
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message
except ImportError:
    sys.exit(
        "需要 ROS 2 Python 环境 (rosbag2_py / rclpy)，未能导入。\n"
        "请先执行: source /opt/ros/humble/setup.bash\n"
        "然后重新运行本脚本。"
    )

TARGET_VERSION = "1.2.0"
SCRIPT_NAME = "migrate_dataset_to_v1_2_0.py"

# 抄自 recordings/gripper_pick_1（当前 1.2.0 recorder 实际录制的视频话题 QoS），
# 旧数据的真实发布者 QoS 已不可回溯，作为固定占位值写入。
VIDEO_OFFERED_QOS_PROFILES_PLACEHOLDER = (
    "- history: 3\n  depth: 0\n  reliability: 1\n  durability: 2\n"
    "  deadline:\n    sec: 9223372036\n    nsec: 854775807\n"
    "  lifespan:\n    sec: 9223372036\n    nsec: 854775807\n"
    "  liveliness: 1\n  liveliness_lease_duration:\n"
    "    sec: 9223372036\n    nsec: 854775807\n"
    "  avoid_ros_namespace_conventions: false"
)

VIDEO_CSV_SUFFIX = "_color_image_raw.csv"
NEW_CSV_COLUMNS = [
    "frame_index",
    "recv_stamp_ns",
    "header_stamp_ns",
    "pts_ns",
    "frame_id",
    "encoding",
    "is_bigendian",
]
FIXED_ENCODING = "rgb8"
FIXED_IS_BIGENDIAN = "0"

MIGRATION_COMMENTS = (
    "迁移说明：旧数据缺少以下信息，转换时采用估算值或固定值补齐。",
    "recv_stamp_ns：匹配 header 时间戳相同的 camera_info 和 metadata 消息，"
    "取两者接收时间的均值；若仅匹配到一条消息，则使用该消息的接收时间。"
    "若均未匹配到，则以图像 header 时间加上本会话中该相机 camera_info 的"
    "接收时间与 header 时间之差的中位数进行估算。",
    "frame_id：使用匹配消息的 header.frame_id，优先选择非空值；"
    "若两者均非空，则优先使用 metadata 的值；若均未匹配到，则留空。",
    "encoding 和 is_bigendian：分别固定为 rgb8 和 0。",
    "视频话题的 offered_qos_profiles 为固定占位值，不代表录制时的真实 QoS。",
)


@dataclass
class CameraFrameRef:
    recv_ns: int
    frame_id: str


@dataclass
class CameraTopicIndex:
    by_header: dict[int, CameraFrameRef] = field(default_factory=dict)
    fallback_offset_ns: int = 0


def find_legacy_sessions(input_root: Path) -> tuple[list[Path], list[Path]]:
    """递归找到所有 session.yaml 所在目录，按是否已有 version 分为待转换/跳过。"""
    to_migrate: list[Path] = []
    to_skip: list[Path] = []
    for session_yaml in sorted(input_root.rglob("session.yaml")):
        session_dir = session_yaml.parent
        with session_yaml.open("r", encoding="utf-8") as f:
            content = yaml.safe_load(f) or {}
        if "version" in content:
            to_skip.append(session_dir)
        else:
            to_migrate.append(session_dir)
    return to_migrate, to_skip


def header_ns_of(msg) -> Optional[int]:
    if not hasattr(msg, "header"):
        return None
    return msg.header.stamp.sec * 1_000_000_000 + msg.header.stamp.nanosec


def build_camera_index(rosbag_dir: Path, camera: str) -> dict[str, CameraTopicIndex]:
    """读取 rosbag 中某相机的 camera_info / metadata，按 header 时间戳建立索引。"""
    topics = {
        "camera_info": f"/woosh/camera/{camera}/color/camera_info",
        "metadata": f"/woosh/camera/{camera}/color/metadata",
    }
    result = {key: CameraTopicIndex() for key in topics}

    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=str(rosbag_dir), storage_id="sqlite3"),
        rosbag2_py.ConverterOptions("", ""),
    )
    type_map = {t.name: t.type for t in reader.get_all_topics_and_types()}
    present = [t for t in topics.values() if t in type_map]
    if not present:
        return result
    reader.set_filter(rosbag2_py.StorageFilter(topics=present))
    msg_classes = {t: get_message(type_map[t]) for t in present}

    topic_to_key = {v: k for k, v in topics.items()}
    ci_recv_minus_header: list[int] = []
    while reader.has_next():
        topic, data, recv_ns = reader.read_next()
        msg = deserialize_message(data, msg_classes[topic])
        h = header_ns_of(msg)
        if h is None:
            continue
        key = topic_to_key[topic]
        result[key].by_header[h] = CameraFrameRef(recv_ns=recv_ns, frame_id=msg.header.frame_id)
        if key == "camera_info":
            ci_recv_minus_header.append(recv_ns - h)

    if ci_recv_minus_header:
        offset = int(statistics.median(ci_recv_minus_header))
        for idx in result.values():
            idx.fallback_offset_ns = offset

    return result


def estimate_recv_and_frame_id(
    header_ns: int,
    camera: str,
    frame_index: str,
    camera_index: dict[str, CameraTopicIndex],
    warnings: list[str],
) -> tuple[int, str]:
    ci_ref = camera_index["camera_info"].by_header.get(header_ns)
    md_ref = camera_index["metadata"].by_header.get(header_ns)

    if ci_ref is not None and md_ref is not None:
        recv_ns = (ci_ref.recv_ns + md_ref.recv_ns) // 2
        if ci_ref.frame_id != md_ref.frame_id:
            warnings.append(
                f"{camera} frame_index={frame_index}: camera_info.frame_id="
                f"{ci_ref.frame_id!r} 与 metadata.frame_id={md_ref.frame_id!r} 不一致"
            )
        frame_id = md_ref.frame_id or ci_ref.frame_id or ""
    elif ci_ref is not None:
        recv_ns = ci_ref.recv_ns
        frame_id = ci_ref.frame_id
    elif md_ref is not None:
        recv_ns = md_ref.recv_ns
        frame_id = md_ref.frame_id
    else:
        offset = camera_index["camera_info"].fallback_offset_ns
        recv_ns = header_ns + offset
        frame_id = ""
        warnings.append(
            f"{camera} frame_index={frame_index}: header_stamp_ns={header_ns} "
            f"在 camera_info/metadata 中均未找到匹配帧，recv_stamp_ns 退化为 "
            f"session 中位数偏移估算 (offset={offset}ns)，frame_id 留空"
        )
    return recv_ns, frame_id


def migrate_video_csv(
    old_csv: Path,
    new_csv: Path,
    camera: str,
    camera_index: dict[str, CameraTopicIndex],
    warnings: list[str],
) -> None:
    with old_csv.open("r", newline="", encoding="utf-8") as f_in:
        rows = list(csv.DictReader(f_in))

    new_csv.parent.mkdir(parents=True, exist_ok=True)
    with new_csv.open("w", newline="", encoding="utf-8") as f_out:
        writer = csv.DictWriter(f_out, fieldnames=NEW_CSV_COLUMNS)
        writer.writeheader()
        for row in rows:
            header_ns = int(row["ros_stamp_ns"])
            recv_ns, frame_id = estimate_recv_and_frame_id(
                header_ns, camera, row["frame_index"], camera_index, warnings
            )
            writer.writerow(
                {
                    "frame_index": row["frame_index"],
                    "recv_stamp_ns": recv_ns,
                    "header_stamp_ns": header_ns,
                    "pts_ns": row["pts_ns"],
                    "frame_id": frame_id,
                    "encoding": FIXED_ENCODING,
                    "is_bigendian": FIXED_IS_BIGENDIAN,
                }
            )


def camera_name_from_csv(csv_path: Path) -> Optional[str]:
    name = csv_path.name
    if not name.endswith(VIDEO_CSV_SUFFIX) or not name.startswith("woosh_camera_"):
        return None
    return name[len("woosh_camera_") : -len(VIDEO_CSV_SUFFIX)]


def migrate_session_yaml(old_yaml: Path, new_yaml: Path) -> None:
    with old_yaml.open("r", encoding="utf-8") as f:
        old_content = yaml.safe_load(f) or {}

    for topic in old_content.get("topics", []):
        if topic.get("backend") == "video":
            topic["offered_qos_profiles"] = VIDEO_OFFERED_QOS_PROFILES_PLACEHOLDER

    # 按新格式实际的字段顺序 (session, version, recorded_at, duration_seconds,
    # topics) 重建，保持与 recorder 当前写出的 session.yaml 一致的可读顺序。
    content = {"session": old_content["session"], "version": TARGET_VERSION}
    for key in ("recorded_at", "duration_seconds", "topics"):
        if key in old_content:
            content[key] = old_content[key]
    content["migrated_from"] = {
        "script": SCRIPT_NAME,
        "migrated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "source": "legacy (no version field)",
    }

    new_yaml.parent.mkdir(parents=True, exist_ok=True)
    with new_yaml.open("w", encoding="utf-8") as f:
        f.write("".join(f"# {line}\n" for line in MIGRATION_COMMENTS))
        yaml.safe_dump(content, f, allow_unicode=True, sort_keys=False, width=float("inf"))


def migrate_session(session_dir: Path, input_root: Path, output_root: Path) -> list[str]:
    """转换单个会话，返回警告信息列表。"""
    warnings: list[str] = []
    rel = session_dir.relative_to(input_root)
    out_dir = output_root / rel

    old_rosbag = session_dir / "rosbag"
    new_rosbag = out_dir / "rosbag"
    if old_rosbag.exists():
        if new_rosbag.exists():
            shutil.rmtree(new_rosbag)
        shutil.copytree(old_rosbag, new_rosbag)

    old_video = session_dir / "video"
    new_video = out_dir / "video"
    if old_video.exists():
        new_video.mkdir(parents=True, exist_ok=True)
        camera_index_cache: dict[str, dict[str, CameraTopicIndex]] = {}
        for item in sorted(old_video.iterdir()):
            if item.suffix == ".csv":
                camera = camera_name_from_csv(item)
                if camera is None:
                    shutil.copy2(item, new_video / item.name)
                    continue
                if camera not in camera_index_cache:
                    camera_index_cache[camera] = build_camera_index(old_rosbag, camera)
                migrate_video_csv(
                    item, new_video / item.name, camera, camera_index_cache[camera], warnings
                )
            else:
                shutil.copy2(item, new_video / item.name)

    migrate_session_yaml(session_dir / "session.yaml", out_dir / "session.yaml")

    return warnings


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--input", required=True, help="旧版本数据集根目录")
    parser.add_argument("--output", required=True, help="转换后数据集输出根目录")
    parser.add_argument(
        "--copy-skipped",
        action="store_true",
        help="原样复制已有 version 字段的会话及会话外的附属文件；输出目录必须不存在",
    )
    args = parser.parse_args()

    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.is_dir():
        sys.exit(f"目录不存在: {input_root}")
    resolved_input = input_root.resolve()
    resolved_output = output_root.resolve()
    if (
        resolved_input.is_relative_to(resolved_output)
        or resolved_output.is_relative_to(resolved_input)
    ):
        parser.error("输入目录与输出目录不能相同或互相包含")
    if args.copy_skipped and output_root.exists():
        parser.error("使用 --copy-skipped 时，输出目录必须不存在，以免覆盖已有数据")

    to_migrate, to_skip = find_legacy_sessions(input_root)

    copied: list[Path] = []
    if args.copy_skipped:
        legacy_dirs = set(to_migrate)

        def ignore_legacy_sessions(directory: str, names: list[str]) -> list[str]:
            return [name for name in names if Path(directory) / name in legacy_dirs]

        print("复制无需转换的会话及附属文件……", flush=True)
        shutil.copytree(input_root, output_root, ignore=ignore_legacy_sessions)
        copied = to_skip
        to_skip = []

    migrated: list[Path] = []
    for session_dir in to_migrate:
        print(f"转换中: {session_dir}")
        warnings = migrate_session(session_dir, input_root, output_root)
        for w in warnings:
            print(f"  [警告] {w}")
        migrated.append(session_dir)

    print("\n" + "=" * 100)
    print(f"成功转换 ({len(migrated)}):")
    for p in migrated:
        print(f"  {p}")

    print(f"\n原样复制 ({len(copied)})（已包含 version 字段，非旧版本）:")
    for p in copied:
        print(f"  {p}")

    print(f"\n跳过 ({len(to_skip)})（已包含 version 字段，非旧版本）:")
    for p in to_skip:
        print(f"  {p}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
