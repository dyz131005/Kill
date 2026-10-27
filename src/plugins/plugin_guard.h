#pragma once

#include <QString>
#include <QStringList>

// =============================================================================
//  PluginGuard —— 插件危险操作的沙箱守卫。
//
//  负责：
//   - 判定某个权限位的等级（Safe / Confirm / Forbidden）
//   - 校验文件路径是否命中黑名单（system32、SAM、游戏目录删除等）
//   - 校验外部进程命令是否命中黑名单（nsudo 提权工具等）
//  Forbidden 一律拒绝，Confirm 需要用户授权（授权存储由 PluginManager 管）。
// =============================================================================
namespace PluginGuard {

// 权限位 -> 等级
int permissionLevel(quint64 perm);

// 该权限组合里是否含有被禁止(Forbidden)的位（含未知/保留位）
bool hasForbiddenPermission(quint64 permissions);

// 校验写/删文件路径。返回 false 并给出 reason 表示被禁止。
// gameDir: 当前游戏目录（删除游戏目录整棵树属于极度危险，禁止）
bool isPathAllowed(const QString &path, bool forDelete, QString *reason);

// 校验要启动的外部程序/命令是否被禁止（如 nsudo 提权、格式化、删除 system32 等）
bool isCommandAllowed(const QString &program, const QStringList &args, QString *reason);

// 危险路径前缀（小写、正斜杠）
QStringList forbiddenPathPrefixes();

// 危险命令关键字（小写）
QStringList forbiddenCommandKeywords();

} // namespace PluginGuard
