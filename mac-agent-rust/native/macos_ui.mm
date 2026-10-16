#include "macos_ui.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

@class SWAgentDelegate;

struct SwuiHandle {
    __strong SWAgentDelegate *delegate;
    void *context;
    SwuiCallback callback;
    SwuiTickCallback tick;
};

namespace {
NSString *string_or(NSDictionary *dictionary, NSString *key, NSString *fallback = @"--") {
    id value = dictionary[key];
    return [value isKindOfClass:[NSString class]] && [value length] ? value : fallback;
}

NSString *number_or(NSDictionary *dictionary, NSString *key, NSString *suffix = @"") {
    id value = dictionary[key];
    if (![value isKindOfClass:[NSNumber class]]) return @"--";
    return [NSString stringWithFormat:@"%@%@", value, suffix];
}

NSDictionary *dictionary_from_json(const char *raw) {
    if (!raw) return @{};
    NSData *data = [[NSString stringWithUTF8String:raw] dataUsingEncoding:NSUTF8StringEncoding];
    if (!data) return @{};
    id value = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingFragmentsAllowed error:nil];
    return [value isKindOfClass:[NSDictionary class]] ? value : @{};
}

NSMenuItem *item(NSString *title, SEL action, id target, NSInteger tag) {
    NSMenuItem *result = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    result.target = target;
    result.tag = tag;
    return result;
}

void replace_menu_items(NSMenu *menu, NSArray<NSMenuItem *> *items) {
    [menu removeAllItems];
    for (NSMenuItem *entry in items) [menu addItem:entry];
}

BOOL uses_simplified_chinese() {
    static const BOOL simplified_chinese = [] {
        NSString *language = NSLocale.preferredLanguages.firstObject ?: @"en";
        return [language.lowercaseString hasPrefix:@"zh"];
    }();
    return simplified_chinese;
}

NSString *L(NSString *english, NSString *chinese) {
    return uses_simplified_chinese() ? chinese : english;
}

NSString *normalized_status(NSString *value) {
    NSString *normalized = [[value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    normalized = [normalized stringByReplacingOccurrencesOfString:@"-" withString:@"_"];
    return [normalized stringByReplacingOccurrencesOfString:@" " withString:@"_"];
}

NSString *localized_token(NSString *value, NSDictionary<NSString *, NSString *> *translations, NSString *fallback) {
    NSString *translated = translations[normalized_status(value ?: @"")];
    return translated ?: fallback;
}

NSString *localized_connection_state(NSString *value) {
    NSDictionary *translations = @{
        @"starting": L(@"Starting", @"正在启动"),
        @"initializing": L(@"Starting", @"正在启动"),
        @"scanning": L(@"Scanning for a watch", @"正在扫描手表"),
        @"not_found": L(@"No watch found", @"未发现手表"),
        @"no_watch_found": L(@"No watch found", @"未发现手表"),
        @"connecting": L(@"Connecting", @"正在连接"),
        @"pairing": L(@"Pairing", @"正在配对"),
        @"waiting_for_code_confirmation": L(@"Waiting for code confirmation", @"等待验证码确认"),
        @"awaiting_code_confirmation": L(@"Waiting for code confirmation", @"等待验证码确认"),
        @"awaiting_approval": L(@"Waiting for watch approval", @"等待手表确认"),
        @"waiting_for_watch_approval": L(@"Waiting for watch approval", @"等待手表确认"),
        @"awaiting_watch_approval": L(@"Waiting for watch approval", @"等待手表确认"),
        @"connected": L(@"Connected", @"已连接"),
        @"live": L(@"Connected", @"已连接"),
        @"ready": L(@"Connected", @"已连接"),
        @"ble_live": L(@"Connected", @"已连接"),
        @"disconnected": L(@"Disconnected", @"已断开连接"),
        @"offline": L(@"Offline", @"离线"),
        @"disabled": L(@"Connection off", @"连接已关闭"),
        @"bluetooth_unavailable": L(@"Bluetooth unavailable", @"蓝牙不可用"),
        @"error": L(@"Connection error", @"连接错误"),
    };
    return localized_token(value, translations, L(@"Unknown status", @"未知状态"));
}

NSString *localized_connection_detail(NSString *value) {
    NSDictionary *translations = @{
        @"Starting Bluetooth": L(@"Starting Bluetooth", @"正在启动蓝牙"),
        @"Searching for StopWatch-Codex": L(@"Searching for StopWatch-Codex", @"正在搜索 StopWatch-Codex"),
        @"Connecting to the watch": L(@"Connecting to the watch", @"正在连接手表"),
        @"Authenticating the authorized watch": L(@"Authenticating the authorized watch", @"正在验证已授权的手表"),
        @"Starting secure watch pairing": L(@"Starting secure watch pairing", @"正在启动安全配对"),
        @"Waiting for approval on the watch": L(@"Waiting for approval on the watch", @"等待手表确认"),
        @"No matching watch was found": L(@"No matching watch was found", @"未发现匹配的手表"),
        @"Bluetooth retry scheduled": L(@"Bluetooth retry scheduled", @"已安排蓝牙重试"),
        @"Bluetooth is unavailable": L(@"Bluetooth is unavailable", @"蓝牙不可用"),
        @"Bluetooth initialization timed out": L(@"Bluetooth initialization timed out", @"蓝牙初始化超时"),
        @"Bluetooth scanning is unavailable": L(@"Bluetooth scanning is unavailable", @"蓝牙扫描不可用"),
        @"Bluetooth scanning timed out": L(@"Bluetooth scanning timed out", @"蓝牙扫描超时"),
        @"Bluetooth is turned off or unavailable": L(@"Bluetooth is turned off or unavailable", @"蓝牙已关闭或不可用"),
        @"Bluetooth adapter state is unavailable": L(@"Bluetooth adapter state is unavailable", @"无法获取蓝牙适配器状态"),
        @"Bluetooth adapter state lookup timed out": L(@"Bluetooth adapter state lookup timed out", @"获取蓝牙适配器状态超时"),
        @"No Bluetooth adapter was found": L(@"No Bluetooth adapter was found", @"未发现蓝牙适配器"),
        @"Bluetooth adapter discovery failed": L(@"Bluetooth adapter discovery failed", @"查找蓝牙适配器失败"),
        @"Bluetooth adapter discovery timed out": L(@"Bluetooth adapter discovery timed out", @"查找蓝牙适配器超时"),
        @"Bluetooth peripheral discovery failed": L(@"Bluetooth peripheral discovery failed", @"查找蓝牙设备失败"),
        @"Bluetooth peripheral discovery timed out": L(@"Bluetooth peripheral discovery timed out", @"查找蓝牙设备超时"),
        @"Watch connection or authentication failed": L(@"Watch connection or authentication failed", @"手表连接或验证失败"),
    };
    return translations[value ?: @""] ?: (value ?: @"");
}

NSString *localized_voice_state(NSString *value) {
    NSDictionary *translations = @{
        @"checking": L(@"Checking", @"正在检查"),
        @"ready": L(@"Ready", @"已就绪"),
        @"recording": L(@"Recording", @"正在录音"),
        @"driver_required": L(@"Setup required", @"需要设置"),
        @"restart_required": L(@"Restart required", @"需要重新启动 Mac"),
        @"accessibility_required": L(@"Accessibility permission required", @"需要辅助功能权限"),
        @"typeless_not_running": L(@"Typeless is not running", @"Typeless 尚未运行"),
        @"unavailable": L(@"Unavailable", @"不可用"),
        @"error": L(@"Error", @"出错"),
    };
    return localized_token(value, translations, L(@"Unknown status", @"未知状态"));
}

NSString *localized_pairing_state(NSString *value) {
    NSDictionary *translations = @{
        @"idle": L(@"Idle", @"空闲"),
        @"pairing": L(@"Pairing", @"正在配对"),
        @"pending": L(@"Pending", @"等待中"),
        @"waiting_for_code_confirmation": L(@"Waiting for code confirmation", @"等待验证码确认"),
        @"awaiting_code_confirmation": L(@"Waiting for code confirmation", @"等待验证码确认"),
        @"awaiting_approval": L(@"Waiting for watch approval", @"等待手表确认"),
        @"waiting_for_watch_approval": L(@"Waiting for watch approval", @"等待手表确认"),
        @"awaiting_watch_approval": L(@"Waiting for watch approval", @"等待手表确认"),
        @"complete": L(@"Complete", @"已完成"),
        @"completed": L(@"Complete", @"已完成"),
        @"error": L(@"Error", @"出错"),
    };
    return localized_token(value, translations, L(@"Unknown status", @"未知状态"));
}

NSString *localized_task_state(NSString *value) {
    NSDictionary *translations = @{
        @"pending": L(@"Pending", @"待处理"),
        @"queued": L(@"Queued", @"排队中"),
        @"running": L(@"Running", @"运行中"),
        @"completed": L(@"Completed", @"已完成"),
        @"handled": L(@"Handled", @"已处理"),
        @"failed": L(@"Failed", @"失败"),
        @"cancelled": L(@"Cancelled", @"已取消"),
        @"canceled": L(@"Cancelled", @"已取消"),
        @"idle": L(@"Idle", @"空闲"),
        @"unknown": L(@"Unknown", @"未知"),
        @"online": L(@"Online", @"在线"),
        @"offline": L(@"Offline", @"离线"),
    };
    NSString *normalized = normalized_status(value ?: @"");
    return translations[normalized] ?: L(@"Other", @"其他");
}

NSString *localized_pet_headline(NSString *value) {
    NSDictionary *translations = @{
        @"choose_a_gif_to_preview": L(@"Choose a GIF to preview", @"选择 GIF 进行预览"),
        @"watch_pet_updated": L(@"Watch pet updated", @"手表桌面宠物已更新"),
        @"cached_watch_pet": L(@"Cached watch pet", @"手表中的宠物"),
        @"cached_pet_preview_failed": L(@"Cached pet preview failed", @"手表宠物预览失败"),
        @"gif_conversion_failed": L(@"GIF conversion failed", @"GIF 转换失败"),
        @"idle": L(@"Idle", @"空闲"),
        @"preparing": L(@"Preparing", @"正在准备"),
        @"sending": L(@"Sending", @"正在发送"),
        @"uploading": L(@"Sending", @"正在发送"),
        @"complete": L(@"Complete", @"已完成"),
        @"error": L(@"Error", @"出错"),
        @"cancelled": L(@"Cancelled", @"已取消"),
        @"ambiguous": L(@"Awaiting confirmation", @"等待确认"),
        @"commit_ambiguous": L(@"Awaiting confirmation", @"等待确认"),
    };
    NSString *translated = translations[normalized_status(value ?: @"")];
    return translated ?: (value ?: @""); // Unknown text may be a user's pet name.
}

NSArray<NSRegularExpression *> *pet_detail_patterns() {
    static NSArray<NSRegularExpression *> *patterns = nil;
    if (!patterns) {
        patterns = @[
            [NSRegularExpression regularExpressionWithPattern:@"^(\\d+) frames · (\\d+) × (\\d+) · ready to send$" options:0 error:nil],
            [NSRegularExpression regularExpressionWithPattern:@"^(\\d+) frames · (\\d+) × (\\d+) · cached watch pet$" options:0 error:nil],
            [NSRegularExpression regularExpressionWithPattern:@"^(\\d+)/(\\d+) bytes · needs (\\d+), available (\\d+)$" options:0 error:nil],
            [NSRegularExpression regularExpressionWithPattern:@"^(\\d+)/(\\d+) bytes acknowledged$" options:0 error:nil],
        ];
    }
    return patterns;
}

NSString *localized_pet_detail(NSString *value) {
    NSString *key = normalized_status(value ?: @"");
    NSDictionary *exact = @{
        @"no_converted_pet_selected": L(@"No converted pet selected", @"尚未选择已转换的宠物"),
        @"no_transfer_in_progress": L(@"No transfer in progress", @"当前没有传输任务"),
        @"watch_confirmed_the_new_pet_package": L(@"Watch confirmed the new pet package", @"手表已确认新的宠物包"),
    };
    NSString *translated = exact[key];
    if (translated) return translated;

    NSString *failurePrefix = @"Transfer failed · ";
    if ([value hasPrefix:failurePrefix]) {
        return [NSString stringWithFormat:L(@"Transfer failed · %@", @"传输失败 · %@"), [value substringFromIndex:failurePrefix.length]];
    }

    NSArray<NSRegularExpression *> *patterns = pet_detail_patterns();
    for (NSUInteger index = 0; index < patterns.count; ++index) {
        NSTextCheckingResult *match = [patterns[index] firstMatchInString:value ?: @"" options:0 range:NSMakeRange(0, (value ?: @"").length)];
        if (!match) continue;
        NSMutableArray<NSString *> *parts = [NSMutableArray array];
        for (NSUInteger group = 1; group < match.numberOfRanges; ++group) {
            [parts addObject:[(value ?: @"") substringWithRange:[match rangeAtIndex:group]]];
        }
        if (index == 0) return [NSString stringWithFormat:L(@"%@ frames · %@ × %@ · ready to send", @"%@ 帧 · %@ × %@ · 可以发送"), parts[0], parts[1], parts[2]];
        if (index == 1) return [NSString stringWithFormat:L(@"%@ frames · %@ × %@ · cached watch pet", @"%@ 帧 · %@ × %@ · 手表中的宠物"), parts[0], parts[1], parts[2]];
        if (index == 2) return [NSString stringWithFormat:L(@"%@/%@ bytes · needs %@, available %@", @"已传输 %@/%@ 字节 · 需要 %@，可用 %@"), parts[0], parts[1], parts[2], parts[3]];
        return [NSString stringWithFormat:L(@"%@/%@ bytes acknowledged", @"已确认 %@/%@ 字节"), parts[0], parts[1]];
    }
    return value ?: @""; // Preserve filenames and diagnostic text verbatim.
}

NSString *path_from_pasteboard(NSPasteboard *pasteboard) {
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if (urls.count != 1 || !urls.firstObject.isFileURL || ![urls.firstObject.pathExtension.lowercaseString isEqualToString:@"gif"]) return nil;
    return urls.firstObject.path;
}

NSUserDefaults *connection_defaults() {
    return NSUserDefaults.standardUserDefaults;
}

bool is_semver(NSString *value) {
    if (![value isKindOfClass:[NSString class]]) return false;
    NSCharacterSet *non_digit = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet];
    NSArray<NSString *> *parts = [value componentsSeparatedByString:@"."];
    if (parts.count != 3) return false;
    for (NSString *part in parts) {
        if (!part.length || [part rangeOfCharacterFromSet:non_digit].location != NSNotFound) return false;
    }
    return true;
}

NSString *application_version() {
    NSBundle *bundle = NSBundle.mainBundle;
    id bundled = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSString *bundled_version = [bundled isKindOfClass:[NSString class]]
        ? [bundled stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] : nil;
    if ([bundle.bundleIdentifier isEqualToString:@"com.stopwatch.codex-agent"] && is_semver(bundled_version)) {
        return bundled_version;
    }
    NSString *resource_root = bundle.resourcePath;
    if (!resource_root.length) return @"dev";
    NSString *resource_path = [resource_root stringByAppendingPathComponent:@"VERSION"];
    NSString *source_version = [NSString stringWithContentsOfFile:resource_path encoding:NSUTF8StringEncoding error:nil];
    source_version = [source_version stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return is_semver(source_version) ? source_version : @"dev";
}
}

@interface SWGifDropView : NSView <NSDraggingDestination>
@property(nonatomic, assign) SwuiHandle *owner;
@property(nonatomic, assign) BOOL highlighted;
@end

@implementation SWGifDropView
- (instancetype)initWithFrame:(NSRect)frame owner:(SwuiHandle *)owner {
    self = [super initWithFrame:frame];
    if (self) {
        _owner = owner;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        self.wantsLayer = YES;
        self.layer.cornerRadius = 9;
        self.layer.borderWidth = 1;
        self.layer.borderColor = NSColor.separatorColor.CGColor;
        self.layer.backgroundColor = NSColor.controlBackgroundColor.CGColor;
    }
    return self;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSString *path = path_from_pasteboard(sender.draggingPasteboard);
    self.highlighted = path != nil;
    self.layer.borderColor = (self.highlighted ? NSColor.controlAccentColor : NSColor.separatorColor).CGColor;
    return self.highlighted ? NSDragOperationCopy : NSDragOperationNone;
}
- (void)draggingExited:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.highlighted = NO;
    self.layer.borderColor = NSColor.separatorColor.CGColor;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSString *path = path_from_pasteboard(sender.draggingPasteboard);
    self.highlighted = NO;
    self.layer.borderColor = NSColor.separatorColor.CGColor;
    if (!path || !_owner || !_owner->callback) return NO;
    _owner->callback(_owner->context, SWUI_PET_CHOOSE, path.UTF8String);
    return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [NSColor.secondaryLabelColor set];
    [L(@"Drop one GIF here", @"将一个 GIF 拖到这里") drawAtPoint:NSMakePoint(14, 14) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:13 weight:NSFontWeightMedium]}];
}
@end

@interface SWAgentDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate> {
@public
    SwuiHandle *_owner;
}
@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *menu;
@property(nonatomic, strong) NSMenu *watchMenu;
@property(nonatomic, strong) NSMenu *codexMenu;
@property(nonatomic, strong) NSMenu *traeMenu;
@property(nonatomic, strong) NSMenu *diagnosticMenu;
@property(nonatomic, strong) NSMenu *petMenu;
@property(nonatomic, strong) NSWindow *petWindow;
@property(nonatomic, strong) NSTextField *petHeadline;
@property(nonatomic, strong) NSTextField *petDetail;
@property(nonatomic, strong) NSProgressIndicator *petProgress;
@property(nonatomic, strong) NSButton *petSend;
@property(nonatomic, strong) NSButton *petCancel;
@property(nonatomic, strong) NSButton *petRestore;
@property(nonatomic, strong) NSImageView *petPreview;
@property(nonatomic, strong) NSTimer *uiTimer;
@property(nonatomic, strong) NSDictionary *snapshot;
@property(nonatomic, strong) NSDictionary *connection;
@property(nonatomic, copy) NSString *pairingCode;
@property(nonatomic, assign) BOOL connectionTogglePending;
@property(nonatomic, assign) BOOL pendingConnectionEnabled;
@property(nonatomic, strong) NSDate *connectionToggleDeadline;
- (instancetype)initWithOwner:(SwuiHandle *)owner;
- (void)updateSnapshot:(NSDictionary *)snapshot connection:(NSDictionary *)connection;
- (void)updatePetHeadline:(NSString *)headline detail:(NSString *)detail progress:(double)progress canSend:(BOOL)canSend canCancel:(BOOL)canCancel canRestore:(BOOL)canRestore;
- (void)setPetPreviewImage:(NSImage *)image;
- (void)showPet;
- (void)emit:(int32_t)command value:(NSString *)value;
- (BOOL)connectionEnabledForDisplay;
- (void)showPendingConnectionEnabled:(BOOL)enabled;
- (void)tick:(NSTimer *)timer;
@end

@implementation SWAgentDelegate
- (instancetype)initWithOwner:(SwuiHandle *)owner {
    self = [super init];
    if (!self) return nil;
    _owner = owner;
    _snapshot = @{};
    _connection = @{};
    _pairingCode = @"--";
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    self.statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    self.statusItem.button.title = @" Codex";
    self.statusItem.button.toolTip = L(@"StopWatch Codex Agent", @"StopWatch Codex Agent");
    self.menu = [[NSMenu alloc] initWithTitle:L(@"StopWatch Codex Agent", @"StopWatch Codex Agent")];
    self.menu.autoenablesItems = NO;
    self.menu.delegate = self;
    self.codexMenu = [[NSMenu alloc] initWithTitle:L(@"Codex Status", @"Codex 状态")];
    self.traeMenu = [[NSMenu alloc] initWithTitle:L(@"Trae Status", @"Trae 状态")];
    self.watchMenu = [[NSMenu alloc] initWithTitle:L(@"Watch", @"手表")];
    self.diagnosticMenu = [[NSMenu alloc] initWithTitle:L(@"Diagnostics", @"诊断")];
    self.petMenu = [[NSMenu alloc] initWithTitle:L(@"Pet", @"桌面宠物")];
    NSMenuItem *agent = item([NSString stringWithFormat:L(@"StopWatch Agent · %@", @"StopWatch Agent · %@"), L(@"Starting", @"正在启动")], nil, self, 100);
    agent.enabled = NO;
    [self.menu addItem:agent];
    NSMenuItem *watch = item([NSString stringWithFormat:L(@"Watch · %@", @"手表 · %@"), localized_connection_state(@"scanning")], nil, self, 110);
    watch.submenu = self.watchMenu;
    [self.menu addItem:watch];
    NSMenuItem *codex = item(@"Codex · --", nil, self, 120);
    codex.submenu = self.codexMenu;
    [self.menu addItem:codex];
    NSMenuItem *trae = item([NSString stringWithFormat:L(@"Trae · %@", @"Trae · %@"), localized_task_state(@"offline")], nil, self, 130);
    trae.submenu = self.traeMenu;
    [self.menu addItem:trae];
    NSMenuItem *pet = item(L(@"Change Desktop GIF…", @"更改桌面 GIF…"), @selector(openPet:), self, SWUI_OPEN_PET);
    [self.menu addItem:pet];
    [self.menu addItem:[NSMenuItem separatorItem]];
    [self.menu addItem:item(L(@"Set Up Watch Voice Input…", @"设置手表语音输入…"), @selector(setupVoice:), self, SWUI_SETUP_VOICE)];
    [self.menu addItem:item(L(@"Open Accessibility Settings…", @"打开辅助功能设置…"), @selector(openAccessibility:), self, 140)];
    NSMenuItem *settings = item(L(@"Agent Settings", @"Agent 设置"), nil, self, 150);
    NSMenu *settingsMenu = [[NSMenu alloc] initWithTitle:L(@"Agent Settings", @"Agent 设置")];
    [settingsMenu addItem:item(L(@"Enable Watch Connection", @"启用手表连接"), @selector(toggleConnection:), self, SWUI_SET_CONNECTION)];
    [settingsMenu addItem:item(L(@"Rename Agent…", @"重命名 Agent…"), @selector(renameAgent:), self, SWUI_RENAME_AGENT)];
    [settingsMenu addItem:item(L(@"Forget This Watch Credential…", @"忘记此手表凭据…"), @selector(forgetWatch:), self, SWUI_FORGET_WATCH)];
    [settingsMenu addItem:item(L(@"Retry Pairing", @"重试配对"), @selector(reconnect:), self, SWUI_RECONNECT)];
    settings.submenu = settingsMenu;
    [self.menu addItem:settings];
    NSMenuItem *diagnostics = item(L(@"Diagnostics", @"诊断"), nil, self, 160);
    [self.diagnosticMenu addItem:item([NSString stringWithFormat:L(@"Version: %@", @"版本：%@"), application_version()], nil, self, 162)];
    [self.diagnosticMenu addItem:item(L(@"Open Log", @"打开日志"), @selector(openLog:), self, 161)];
    diagnostics.submenu = self.diagnosticMenu;
    [self.menu addItem:diagnostics];
    [self.menu addItem:[NSMenuItem separatorItem]];
    [self.menu addItem:item(L(@"Reconnect Now", @"立即重新连接"), @selector(reconnect:), self, SWUI_RECONNECT)];
    [self.menu addItem:item(L(@"Quit StopWatch Codex Agent", @"退出 StopWatch Codex Agent"), @selector(quitAgent:), self, SWUI_QUIT)];
    self.statusItem.menu = self.menu;
    [self updateSnapshot:self.snapshot connection:self.connection];
    self.uiTimer = [NSTimer scheduledTimerWithTimeInterval:0.1
                                                   target:self
                                                 selector:@selector(tick:)
                                                 userInfo:nil
                                                  repeats:YES];
}

- (void)tick:(NSTimer *)timer {
    (void)timer;
    if (_owner && _owner->tick) _owner->tick(_owner->context, _owner);
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [self.uiTimer invalidate];
    self.uiTimer = nil;
}

- (void)emit:(int32_t)command value:(NSString *)value {
    if (_owner && _owner->callback) _owner->callback(_owner->context, command, value.UTF8String);
}

- (void)reconnect:(id)sender {
    (void)sender;
    [self showPendingConnectionEnabled:YES];
    [self emit:SWUI_RECONNECT value:@""];
}
- (void)setupVoice:(id)sender {
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = L(@"Set Up Watch Voice Input?", @"要设置手表语音输入吗？");
    alert.informativeText = L(@"BlackHole requires administrator authorization. A Mac restart may be required.", @"BlackHole 需要管理员授权，可能还需要重新启动 Mac。");
    [alert addButtonWithTitle:L(@"Install", @"安装")];
    [alert addButtonWithTitle:L(@"Cancel", @"取消")];
    if ([alert runModal] == NSAlertFirstButtonReturn) [self emit:SWUI_SETUP_VOICE value:@""];
}
- (void)openAccessibility:(id)sender {
    (void)sender;
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
    if (url) [NSWorkspace.sharedWorkspace openURL:url];
}
- (BOOL)connectionEnabledForDisplay {
    id runtimeValue = self.connection[@"connection_enabled"];
    BOOL enabled = [runtimeValue isKindOfClass:[NSNumber class]] ? [runtimeValue boolValue] : swui_connection_enabled() == 1;
    if (self.connectionTogglePending) {
        if ([runtimeValue isKindOfClass:[NSNumber class]] && enabled == self.pendingConnectionEnabled) {
            self.connectionTogglePending = NO;
            self.connectionToggleDeadline = nil;
        } else if (self.connectionToggleDeadline.timeIntervalSinceNow > 0) {
            return self.pendingConnectionEnabled;
        } else {
            self.connectionTogglePending = NO;
            self.connectionToggleDeadline = nil;
        }
    }
    return enabled;
}
- (void)showPendingConnectionEnabled:(BOOL)enabled {
    self.connectionTogglePending = YES;
    self.pendingConnectionEnabled = enabled;
    self.connectionToggleDeadline = [NSDate dateWithTimeIntervalSinceNow:6.0];
    NSMenuItem *settings = [self.menu itemWithTag:150];
    NSMenuItem *connectionItem = [settings.submenu itemWithTag:SWUI_SET_CONNECTION];
    connectionItem.state = enabled ? NSControlStateValueOn : NSControlStateValueOff;
    [self updateSnapshot:self.snapshot connection:self.connection];
}
- (void)toggleConnection:(NSMenuItem *)sender {
    (void)sender;
    BOOL enabled = ![self connectionEnabledForDisplay];
    [self showPendingConnectionEnabled:enabled];
    [self emit:SWUI_SET_CONNECTION value:(enabled ? @"true" : @"false")];
}
- (void)renameAgent:(id)sender {
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = L(@"Name This Agent", @"为此 Agent 命名");
    alert.informativeText = L(@"This name is signed by this Mac and shown in the watch Agent list.", @"此名称由这台 Mac 签名，并显示在手表的 Agent 列表中。");
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 280, 24)];
    field.stringValue = string_or(self.connection, @"agent_name", @"StopWatch Codex");
    alert.accessoryView = field;
    [alert addButtonWithTitle:L(@"Save", @"保存")];
    [alert addButtonWithTitle:L(@"Cancel", @"取消")];
    if ([alert runModal] == NSAlertFirstButtonReturn) [self emit:SWUI_RENAME_AGENT value:field.stringValue];
}
- (void)forgetWatch:(id)sender {
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = L(@"Forget This Watch Credential?", @"要忘记此手表凭据吗？");
    alert.informativeText = L(@"The Agent will reconnect and require matching the six-digit code plus physical approval on the watch before receiving a new credential.", @"Agent 将重新连接；获取新凭据前，需要核对六位验证码并在手表上确认。");
    [alert addButtonWithTitle:L(@"Forget and Re-pair", @"忘记并重新配对")];
    [alert addButtonWithTitle:L(@"Cancel", @"取消")];
    if ([alert runModal] == NSAlertFirstButtonReturn) [self emit:SWUI_FORGET_WATCH value:@""]; 
}
- (void)openPet:(id)sender { (void)sender; [self showPet]; }
- (void)quitAgent:(id)sender {
    (void)sender;
    [self emit:SWUI_QUIT value:@""];
    NSApplication *application = NSApplication.sharedApplication;
    [application stop:nil];
    NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
    if (wake) [application postEvent:wake atStart:YES];
}
- (void)openLog:(id)sender {
    (void)sender;
    NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/stopwatch-codex-agent.log"];
    BOOL isDirectory = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDirectory] || isDirectory) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = L(@"Log file not found", @"未找到日志文件");
        alert.informativeText = L(@"The Agent has not created its log yet.", @"Agent 尚未创建日志文件。");
        [alert addButtonWithTitle:L(@"OK", @"好")];
        [alert runModal];
        return;
    }
    if (![NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:path]]) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = L(@"Could not open the log", @"无法打开日志");
        alert.informativeText = path;
        [alert addButtonWithTitle:L(@"OK", @"好")];
        [alert runModal];
    }
}
- (void)selectWatch:(NSMenuItem *)sender { [self emit:SWUI_SELECT_WATCH value:[sender.representedObject isKindOfClass:[NSString class]] ? sender.representedObject : @""]; }
- (void)choosePet:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = @[];
    panel.allowedFileTypes = @[@"gif"];
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL.pathExtension.lowercaseString.length && [panel.URL.pathExtension.lowercaseString isEqualToString:@"gif"]) {
            [self emit:SWUI_PET_CHOOSE value:panel.URL.path];
        }
    }];
}
- (void)sendPet:(id)sender { (void)sender; [self emit:SWUI_PET_SEND value:@""]; }
- (void)cancelPet:(id)sender { (void)sender; [self emit:SWUI_PET_CANCEL value:@""]; }
- (void)restorePet:(id)sender { (void)sender; [self emit:SWUI_PET_RESTORE value:@""]; }

- (void)ensurePetWindow {
    if (self.petWindow) return;
    const NSRect frame = NSMakeRect(0, 0, 480, 560);
    self.petWindow = [[NSWindow alloc] initWithContentRect:frame styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];
    self.petWindow.title = L(@"Codex Pet", @"Codex 桌面宠物");
    self.petWindow.releasedWhenClosed = NO;
    self.petWindow.contentMinSize = NSMakeSize(480, 560);
    self.petWindow.contentMaxSize = NSMakeSize(560, 680);
    self.petWindow.backgroundColor = [NSColor colorWithCalibratedRed:0.082 green:0.094 blue:0.129 alpha:1.0];
    NSView *content = self.petWindow.contentView;
    NSTextField *(^label)(NSRect, NSString *, CGFloat) = ^NSTextField *(NSRect rect, NSString *value, CGFloat size) {
        NSTextField *field = [NSTextField labelWithString:value];
        field.frame = rect;
        field.font = [NSFont systemFontOfSize:size];
        field.textColor = NSColor.labelColor;
        field.lineBreakMode = NSLineBreakByTruncatingTail;
        field.usesSingleLineMode = YES;
        return field;
    };
    [content addSubview:label(NSMakeRect(24, 520, 432, 24), L(@"Codex Pet", @"Codex 桌面宠物"), 20)];
    [content addSubview:label(NSMakeRect(24, 496, 432, 18), L(@"Preview the watch pet package and send it to the connected watch.", @"预览手表宠物资源包，并发送到已连接的手表。"), 12)];
    self.petPreview = [[NSImageView alloc] initWithFrame:NSMakeRect(36, 280, 190, 190)];
    self.petPreview.imageScaling = NSImageScaleProportionallyUpOrDown;
    [content addSubview:self.petPreview];
    self.petHeadline = label(NSMakeRect(250, 410, 205, 40), L(@"Choose a GIF to preview", @"选择 GIF 进行预览"), 16);
    self.petHeadline.maximumNumberOfLines = 2;
    self.petDetail = label(NSMakeRect(250, 340, 205, 60), L(@"No converted pet selected", @"尚未选择已转换的宠物"), 12);
    self.petDetail.maximumNumberOfLines = 4;
    [content addSubview:self.petHeadline];
    [content addSubview:self.petDetail];
    SWGifDropView *drop = [[SWGifDropView alloc] initWithFrame:NSMakeRect(24, 200, 432, 58) owner:_owner];
    [content addSubview:drop];
    NSButton *(^button)(NSRect, NSString *, SEL) = ^NSButton *(NSRect rect, NSString *title, SEL selector) {
        NSButton *result = [[NSButton alloc] initWithFrame:rect];
        result.title = title;
        result.bezelStyle = NSBezelStyleRounded;
        result.target = self;
        result.action = selector;
        return result;
    };
    NSButton *choose = button(NSMakeRect(160, 211, 150, 32), L(@"Choose GIF…", @"选择 GIF…"), @selector(choosePet:));
    [content addSubview:choose];
    NSTextField *connection = label(NSMakeRect(24, 168, 432, 20), L(@"Watch offline — preview is still available", @"手表未连接，仍可预览"), 12);
    connection.tag = 501;
    [content addSubview:connection];
    self.petProgress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, 140, 432, 8)];
    self.petProgress.indeterminate = NO;
    self.petProgress.minValue = 0;
    self.petProgress.maxValue = 1;
    self.petProgress.doubleValue = 0;
    [content addSubview:self.petProgress];
    self.petDetail = label(NSMakeRect(24, 108, 432, 24), L(@"No transfer in progress", @"当前没有传输任务"), 11);
    [content addSubview:self.petDetail];
    self.petRestore = button(NSMakeRect(24, 50, 128, 32), L(@"Restore Default", @"恢复默认"), @selector(restorePet:));
    self.petCancel = button(NSMakeRect(24, 50, 128, 32), L(@"Cancel", @"取消"), @selector(cancelPet:));
    self.petSend = button(NSMakeRect(306, 50, 150, 32), L(@"Send to Watch", @"发送到手表"), @selector(sendPet:));
    [content addSubview:self.petRestore];
    [content addSubview:self.petCancel];
    [content addSubview:self.petSend];
}

- (void)showPet {
    [self ensurePetWindow];
    [self.petWindow makeKeyAndOrderFront:nil];
    [NSApplication.sharedApplication activateIgnoringOtherApps:YES];
}

- (void)updateSnapshot:(NSDictionary *)snapshot connection:(NSDictionary *)connection {
    self.snapshot = [snapshot isKindOfClass:[NSDictionary class]] ? snapshot : @{};
    self.connection = [connection isKindOfClass:[NSDictionary class]] ? connection : @{};
    NSDictionary *codex = [self.snapshot[@"codex"] isKindOfClass:[NSDictionary class]] ? self.snapshot[@"codex"] : @{};
    NSDictionary *trae = [self.snapshot[@"trae"] isKindOfClass:[NSDictionary class]] ? self.snapshot[@"trae"] : @{};
    NSDictionary *telemetry = [self.snapshot[@"telemetry"] isKindOfClass:[NSDictionary class]] ? self.snapshot[@"telemetry"] : @{};
    NSDictionary *inventory = [telemetry[@"agent_inventory"] isKindOfClass:[NSDictionary class]] ? telemetry[@"agent_inventory"] : nil;
    NSArray *installedAgents = [inventory[@"agents"] isKindOfClass:[NSArray class]] ? inventory[@"agents"] : nil;
    id rawInventoryVersion = inventory[@"v"];
    BOOL integerInventoryVersion = [rawInventoryVersion isKindOfClass:[NSNumber class]];
    if (integerInventoryVersion) {
        CFTypeRef number = (__bridge CFTypeRef)rawInventoryVersion;
        integerInventoryVersion = CFGetTypeID(number) != CFBooleanGetTypeID()
            && !CFNumberIsFloatType((CFNumberRef)number);
    }
    BOOL validInventoryEntries = installedAgents != nil && installedAgents.count <= 8;
    NSMutableSet<NSString *> *seenAgentIds = [NSMutableSet set];
    for (id rawAgentId in installedAgents) {
        if (![rawAgentId isKindOfClass:[NSString class]]) {
            validInventoryEntries = NO;
            break;
        }
        NSString *agentId = rawAgentId;
        NSData *agentIdBytes = [agentId dataUsingEncoding:NSASCIIStringEncoding allowLossyConversion:NO];
        BOOL validId = agentIdBytes != nil && agentIdBytes.length > 0 && agentIdBytes.length <= 16;
        const uint8_t *bytes = (const uint8_t *)agentIdBytes.bytes;
        for (NSUInteger index = 0; validId && index < agentIdBytes.length; ++index) {
            validId = bytes[index] >= 0x21 && bytes[index] <= 0x7e;
        }
        if (!validId || [seenAgentIds containsObject:agentId]) {
            validInventoryEntries = NO;
            break;
        }
        [seenAgentIds addObject:agentId];
    }
    BOOL inventoryKnown = integerInventoryVersion
        && [rawInventoryVersion unsignedLongLongValue] == 1
        && validInventoryEntries;
    BOOL connectionEnabled = [self connectionEnabledForDisplay];
    NSString *rawState = string_or(self.connection, @"state", @"scanning");
    NSString *state;
    if (self.connectionTogglePending) {
        state = connectionEnabled ? L(@"Enabling connection…", @"正在启用连接…") : localized_connection_state(@"disabled");
    } else {
        state = localized_connection_state(connectionEnabled ? rawState : @"disabled");
    }
    NSString *rawVoice = string_or(self.connection, @"voice_state", @"checking");
    NSString *voice = localized_voice_state(rawVoice);
    NSString *agentName = string_or(self.connection, @"agent_name", @"StopWatch Codex");
    BOOL traeOnline = [trae[@"trae_online"] isKindOfClass:[NSNumber class]] && [trae[@"trae_online"] boolValue];
    NSString *traeStatus = localized_task_state(traeOnline ? @"online" : @"offline");
    NSMenuItem *agent = [self.menu itemWithTag:100];
    agent.title = [NSString stringWithFormat:L(@"%@ · %@ · Voice %@", @"%@ · %@ · 语音 %@"), agentName, state, voice];
    NSMenuItem *watch = [self.menu itemWithTag:110];
    watch.title = [NSString stringWithFormat:L(@"Watch · %@ · Pairing %@ · Code %@", @"手表 · %@ · 配对 %@ · 验证码 %@"), state, localized_pairing_state(string_or(self.connection, @"pairing_state", @"idle")), self.pairingCode];
    NSMenuItem *codexRoot = [self.menu itemWithTag:120];
    if (inventoryKnown) codexRoot.hidden = ![installedAgents containsObject:@"codex"];
    codexRoot.title = [NSString stringWithFormat:L(@"Codex · %@ · %@ · %@ · %@ Pending / %@ Running / %@ Completed", @"Codex · %@ · %@ · %@ · %@ 待处理 / %@ 运行中 / %@ 已完成"),
        string_or(codex, @"codex_model"), number_or(codex, @"codex_5h_left_pct", @"%"), number_or(codex, @"codex_week_left_pct", @"%"),
        number_or(codex, @"codex_alert_pending"), number_or(codex, @"codex_alert_running"), number_or(codex, @"codex_alert_completed")];
    NSMenuItem *traeRoot = [self.menu itemWithTag:130];
    if (inventoryKnown) traeRoot.hidden = ![installedAgents containsObject:@"trae"];
    traeRoot.title = [NSString stringWithFormat:@"Trae · %@ · %@", traeStatus, string_or(trae, @"trae_model")];

    NSMutableArray<NSMenuItem *> *codexRows = [NSMutableArray array];
    [codexRows addObject:item([NSString stringWithFormat:L(@"Model: %@", @"模型：%@"), string_or(codex, @"codex_model")], nil, self, 201)];
    [codexRows addObject:item([NSString stringWithFormat:L(@"Effort: %@", @"推理强度：%@"), string_or(codex, @"codex_effort")], nil, self, 202)];
    [codexRows addObject:item([NSString stringWithFormat:L(@"5H Remaining: %@", @"5 小时额度剩余：%@"), number_or(codex, @"codex_5h_left_pct", @"%")], nil, self, 203)];
    [codexRows addObject:item([NSString stringWithFormat:L(@"7D Remaining: %@", @"7 天额度剩余：%@"), number_or(codex, @"codex_week_left_pct", @"%")], nil, self, 204)];
    [codexRows addObject:item([NSString stringWithFormat:L(@"Tasks: %@ pending · %@ running · %@ completed", @"任务：%@ 待处理 · %@ 运行中 · %@ 已完成"),
        number_or(codex, @"codex_alert_pending"), number_or(codex, @"codex_alert_running"), number_or(codex, @"codex_alert_completed")], nil, self, 205)];
    replace_menu_items(self.codexMenu, codexRows);

    NSMutableArray<NSMenuItem *> *traeRows = [NSMutableArray array];
    NSDictionary *traeLabels = @{
        @"trae_online": L(@"Status", @"状态"),
        @"trae_model": L(@"Model", @"模型"),
        @"trae_effort": L(@"Effort", @"推理强度"),
        @"trae_project": L(@"Project", @"项目"),
        @"trae_state": L(@"Task state", @"任务状态"),
        @"trae_task": L(@"Task", @"任务"),
        @"trae_detail": L(@"Details", @"详情"),
    };
    for (NSString *key in @[@"trae_online", @"trae_model", @"trae_effort", @"trae_project", @"trae_state", @"trae_task", @"trae_detail"]) {
        NSString *value = string_or(trae, key);
        if ([key isEqualToString:@"trae_online"]) value = traeStatus;
        if ([key isEqualToString:@"trae_state"]) value = localized_task_state(string_or(trae, key, @"unknown"));
        [traeRows addObject:item([NSString stringWithFormat:L(@"%@: %@", @"%@：%@"), traeLabels[key], value], nil, self, 301)];
    }
    replace_menu_items(self.traeMenu, traeRows);

    NSMutableArray<NSMenuItem *> *watchRows = [NSMutableArray array];
    NSArray *watches = [self.connection[@"watches"] isKindOfClass:[NSArray class]] ? self.connection[@"watches"] : @[];
    if (!watches.count) [watchRows addObject:item([NSString stringWithFormat:L(@"Bluetooth: %@", @"蓝牙：%@"), state], nil, self, 401)];
    for (NSDictionary *entry in watches) {
        if (![entry isKindOfClass:[NSDictionary class]]) continue;
        NSString *identifier = string_or(entry, @"id", @"");
        NSMenuItem *watchItem = item(string_or(entry, @"name", identifier), @selector(selectWatch:), self, SWUI_SELECT_WATCH);
        watchItem.representedObject = identifier;
        watchItem.state = [identifier isEqualToString:string_or(self.connection, @"selected_watch_id", @"")] ? NSControlStateValueOn : NSControlStateValueOff;
        [watchRows addObject:watchItem];
    }
    [watchRows addObject:[NSMenuItem separatorItem]];
    [watchRows addObject:item([NSString stringWithFormat:L(@"Connection: %@", @"连接：%@"), state], nil, self, 402)];
    [watchRows addObject:item([NSString stringWithFormat:L(@"Agent: %@", @"Agent：%@"), agentName], nil, self, 403)];
    [watchRows addObject:item([NSString stringWithFormat:L(@"Pairing: %@", @"配对：%@"), localized_pairing_state(string_or(self.connection, @"pairing_state", @"idle"))], nil, self, 404)];
    [watchRows addObject:item([NSString stringWithFormat:L(@"Verification code: %@", @"验证码：%@"), self.pairingCode], nil, self, 405)];
    [watchRows addObject:item([NSString stringWithFormat:L(@"Voice input: %@", @"语音输入：%@"), voice], nil, self, 406)];
    NSString *detail = string_or(self.connection, @"detail", @"");
    NSString *error = string_or(self.connection, @"error", @"");
    NSString *voiceDetail = string_or(self.connection, @"voice_detail", @"");
    if (detail.length) [watchRows addObject:item([NSString stringWithFormat:L(@"Details: %@", @"详情：%@"), localized_connection_detail(detail)], nil, self, 407)];
    if (error.length) [watchRows addObject:item([NSString stringWithFormat:L(@"Error: %@", @"错误：%@"), error], nil, self, 408)];
    if (voiceDetail.length) [watchRows addObject:item([NSString stringWithFormat:L(@"Voice details: %@", @"语音详情：%@"), voiceDetail], nil, self, 409)];
    NSString *notice = string_or(self.connection, @"update_notice", @"");
    if (notice.length) [watchRows addObject:item([NSString stringWithFormat:L(@"Update: %@", @"更新：%@"), notice], nil, self, 410)];
    replace_menu_items(self.watchMenu, watchRows);

    NSMenuItem *settings = [self.menu itemWithTag:150];
    NSMenuItem *connectionItem = [settings.submenu itemWithTag:SWUI_SET_CONNECTION];
    if (connectionItem) connectionItem.state = connectionEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    NSMenuItem *setup = [self.menu itemWithTag:SWUI_SETUP_VOICE];
    setup.hidden = ![rawVoice isEqualToString:@"driver_required"];
    [self updatePetHeadline:string_or(self.connection, @"pet_headline", @"Choose a GIF to preview")
        detail:string_or(self.connection, @"pet_detail", @"No converted pet selected")
        progress:[self.connection[@"pet_progress"] doubleValue]
        canSend:[self.connection[@"pet_can_send"] boolValue]
        canCancel:[self.connection[@"pet_can_cancel"] boolValue]
        canRestore:[self.connection[@"pet_can_restore"] boolValue]];
    if (self.petWindow) {
        NSTextField *connectionLabel = [self.petWindow.contentView viewWithTag:501];
        NSString *petConnection = string_or(self.connection, @"pet_connection", @"");
        if (!petConnection.length) petConnection = L(@"Preview available", @"仍可预览");
        else petConnection = localized_connection_state(petConnection);
        if (connectionLabel) connectionLabel.stringValue = [NSString stringWithFormat:L(@"%@ · %@", @"%@ · %@"), state, petConnection];
    }
}

- (void)updatePetHeadline:(NSString *)headline detail:(NSString *)detail progress:(double)progress canSend:(BOOL)canSend canCancel:(BOOL)canCancel canRestore:(BOOL)canRestore {
    if (!self.petWindow) return;
    self.petHeadline.stringValue = localized_pet_headline(headline);
    self.petDetail.stringValue = localized_pet_detail(detail);
    self.petProgress.doubleValue = std::isfinite(progress) ? std::clamp(progress, 0.0, 1.0) : 0.0;
    self.petSend.enabled = canSend;
    self.petCancel.enabled = canCancel;
    self.petCancel.hidden = !canCancel;
    self.petRestore.enabled = canRestore;
    self.petRestore.hidden = canCancel;
}
- (void)setPetPreviewImage:(NSImage *)image { if (self.petPreview) self.petPreview.image = image; }
@end

extern "C" SwuiHandle *swui_create(void *context, SwuiCallback callback, SwuiTickCallback tick) {
    if (!callback || !tick || !NSThread.isMainThread) return nullptr;
    @autoreleasepool {
        SwuiHandle *handle = new SwuiHandle();
        handle->delegate = [[SWAgentDelegate alloc] initWithOwner:handle];
        handle->context = context;
        handle->callback = callback;
        handle->tick = tick;
        return handle;
    }
}

extern "C" int32_t swui_run(SwuiHandle *handle) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return -1;
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        application.activationPolicy = NSApplicationActivationPolicyAccessory;
        application.delegate = handle->delegate;
        [application run];
    }
    return 0;
}

extern "C" void swui_update(SwuiHandle *handle, const char *snapshot_json, const char *connection_json) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return;
    NSDictionary *snapshot = dictionary_from_json(snapshot_json);
    NSDictionary *connection = dictionary_from_json(connection_json);
    SWAgentDelegate *delegate = handle->delegate;
    @autoreleasepool { [delegate updateSnapshot:snapshot connection:connection]; }
}

extern "C" void swui_pairing_code(SwuiHandle *handle, const char *code) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return;
    NSString *value = code ? [NSString stringWithUTF8String:code] : @"";
    SWAgentDelegate *delegate = handle->delegate;
    @autoreleasepool { delegate.pairingCode = value.length == 6 ? value : @"--"; [delegate updateSnapshot:delegate.snapshot connection:delegate.connection]; }
}

extern "C" void swui_pet_state(SwuiHandle *handle, const char *headline, const char *detail, double progress, int32_t can_send, int32_t can_cancel, int32_t can_restore) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return;
    NSString *headline_value = headline ? [NSString stringWithUTF8String:headline] : @"";
    NSString *detail_value = detail ? [NSString stringWithUTF8String:detail] : @"";
    SWAgentDelegate *delegate = handle->delegate;
    @autoreleasepool { [delegate updatePetHeadline:headline_value detail:detail_value progress:progress canSend:can_send == 1 canCancel:can_cancel == 1 canRestore:can_restore == 1]; }
}

extern "C" void swui_pet_preview_rgba(SwuiHandle *handle, const uint8_t *bytes, size_t byte_count, int32_t width, int32_t height) {
    if (!handle || !handle->delegate || !NSThread.isMainThread || !bytes || width <= 0 || height <= 0 || width > 512 || height > 512 || byte_count != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) return;
    NSData *data = [NSData dataWithBytes:bytes length:byte_count];
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:width * 4 bitsPerPixel:32];
    if (!bitmap) return;
    memcpy(bitmap.bitmapData, data.bytes, byte_count);
    [image addRepresentation:bitmap];
    SWAgentDelegate *delegate = handle->delegate;
    @autoreleasepool { [delegate setPetPreviewImage:image]; }
}

extern "C" void swui_show_pet(SwuiHandle *handle) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return;
    SWAgentDelegate *delegate = handle->delegate;
    @autoreleasepool { [delegate showPet]; }
}

extern "C" int32_t swui_pet_preview_visible(SwuiHandle *handle) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return 0;
    return handle->delegate.petWindow.isVisible ? 1 : 0;
}

extern "C" void swui_shutdown(SwuiHandle *handle) {
    if (!handle || !handle->delegate || !NSThread.isMainThread) return;
    SWAgentDelegate *delegate = handle->delegate;
    @autoreleasepool {
        [delegate.uiTimer invalidate];
        delegate.uiTimer = nil;
        [delegate.statusItem.menu cancelTracking];
        [NSStatusBar.systemStatusBar removeStatusItem:delegate.statusItem];
        delegate.statusItem = nil;
    }
}

extern "C" void swui_destroy(SwuiHandle *handle) {
    if (handle && NSThread.isMainThread) delete handle;
}

extern "C" int32_t swui_connection_enabled(void) {
    @autoreleasepool {
        NSUserDefaults *defaults = connection_defaults();
        [defaults registerDefaults:@{@"connectionEnabled":@YES}];
        return [defaults boolForKey:@"connectionEnabled"] ? 1 : 0;
    }
}

extern "C" void swui_set_connection_enabled(int32_t enabled) {
    @autoreleasepool {
        NSUserDefaults *defaults = connection_defaults();
        [defaults registerDefaults:@{@"connectionEnabled":@YES}];
        [defaults setBool:enabled == 1 forKey:@"connectionEnabled"];
    }
}
