/**
 * i18n — minimal internationalization layer.
 * Supports English (default) and Arabic (RTL).
 * Only UI chrome strings are translated — dynamic data like achievement
 * names, log lines, and DLC titles are left as-is.
 */

export type Locale = "en" | "ar";

const strings: Record<string, Record<Locale, string>> = {
  // Titlebar
  "titlebar.title": { en: "Epic Unlocker — Achievement Manager", ar: "Epic Unlocker — مدير الإنجازات" },

  // Menubar
  "menu.achievements": { en: "Achievements", ar: "الإنجازات" },
  "menu.dlc": { en: "DLC", ar: "المحتوى الإضافي" },
  "menu.log": { en: "Log", ar: "السجل" },
  "menu.settings": { en: "Settings", ar: "الإعدادات" },

  // Sidebar
  "sidebar.unlocked": { en: "Unlocked", ar: "مفتوح" },
  "sidebar.locked": { en: "Locked", ar: "مقفل" },
  "sidebar.statGated": { en: "Stat-gated", ar: "مرتبط بإحصائية" },
  "sidebar.progress": { en: "Progress", ar: "التقدم" },
  "sidebar.unlockAll": { en: "Unlock All (Ctrl+Shift+U)", ar: "فتح الكل (Ctrl+Shift+U)" },
  "sidebar.refresh": { en: "Refresh", ar: "تحديث" },
  "sidebar.fetchIcons": { en: "Fetch Icons", ar: "جلب الأيقونات" },
  "sidebar.fetchRarity": { en: "Fetch Rarity", ar: "جلب الندرة" },
  "sidebar.clearCache": { en: "Clear Icon Cache", ar: "مسح ذاكرة الأيقونات" },
  "sidebar.connecting": { en: "Connecting…", ar: "جاري الاتصال…" },
  "sidebar.notConnected": { en: "Not connected", ar: "غير متصل" },
  "sidebar.noGame": { en: "No Game", ar: "لا توجد لعبة" },

  // Achievements tab
  "ach.filterAll": { en: "All", ar: "الكل" },
  "ach.filterUnlocked": { en: "Unlocked", ar: "مفتوح" },
  "ach.filterLocked": { en: "Locked", ar: "مقفل" },
  "ach.filterHidden": { en: "Hidden", ar: "مخفي" },
  "ach.searchPlaceholder": { en: "Search achievements…", ar: "بحث في الإنجازات…" },
  "ach.badgeUnlocked": { en: "Unlocked", ar: "مفتوح" },
  "ach.badgeLocked": { en: "Locked", ar: "مقفل" },
  "ach.badgeHidden": { en: "Hidden", ar: "مخفي" },
  "ach.badgeStatGated": { en: "Stat-gated", ar: "مرتبط بإحصائية" },
  "ach.btnUnlock": { en: "Unlock", ar: "فتح" },
  "ach.btnDone": { en: "Done", ar: "تم" },
  "ach.rarityBronze": { en: "Bronze", ar: "برونزي" },
  "ach.raritySilver": { en: "Silver", ar: "فضي" },
  "ach.rarityGold": { en: "Gold", ar: "ذهبي" },
  "ach.rarityPlatinum": { en: "Platinum", ar: "بلاتيني" },
  "ach.emptyConnecting": { en: "Connecting...", ar: "جاري الاتصال..." },
  "ach.emptyConnectingBody": { en: "Waiting for the Epic Unlocker pipe to deliver the achievement list.", ar: "في انتظار أنموذج Epic Unlocker لتسليم قائمة الإنجازات." },
  "ach.emptyNotConnected": { en: "Not connected", ar: "غير متصل" },
  "ach.emptyNotConnectedBody": { en: "Launch a game with Epic Unlocker injected to establish the pipe connection.", ar: "قم بتشغيل لعبة مع Epic Unlocker محقون لتأسيس اتصال الأنموذج." },
  "ach.emptyNoAch": { en: "No achievements", ar: "لا توجد إنجازات" },
  "ach.emptyNoAchBody": { en: "The pipe is connected but no achievement definitions have been received yet. Try Refresh.", ar: "الأنموذج متصل ولكن لم يتم استلام تعريفات الإنجازات بعد. جرب التحديث." },
  "ach.emptyNoMatches": { en: "No matches", ar: "لا توجد نتائج" },
  "ach.emptyNoMatchesBody": { en: "No achievements match your current search and filter combination.", ar: "لا توجد إنجازات تطابق بحثك وفلترك الحالي." },
  "ach.timeToday": { en: "Today", ar: "اليوم" },
  "ach.timeYesterday": { en: "Yesterday", ar: "أمس" },
  "ach.timeDaysAgo": { en: "d ago", ar: "يوم مضت" },
  "ach.timeMonthsAgo": { en: "mo ago", ar: "شهر مضت" },
  "ach.timeYearsAgo": { en: "y ago", ar: "سنة مضت" },
  "ach.sortAlpha": { en: "Alphabetical", ar: "أبجدي" },
  "ach.sortRarity": { en: "Rarity", ar: "الندرة" },
  "ach.sortProgressHigh": { en: "Progress: High to Low", ar: "التقدم: من الأعلى إلى الأدنى" },
  "ach.sortProgressLow": { en: "Progress: Low to High", ar: "التقدم: من الأدنى إلى الأعلى" },
  "ach.sortXpHigh": { en: "XP: High to Low", ar: "XP: من الأعلى إلى الأدنى" },
  "ach.sortXpLow": { en: "XP: Low to High", ar: "XP: من الأدنى إلى الأعلى" },
  "ach.sortDefault": { en: "Default", ar: "افتراضي" },
  "ach.sortLabel": { en: "Sort achievements", ar: "ترتيب الإنجازات" },
  "ach.copyIdHint": { en: "Right-click to copy ID: {id}", ar: "انقر بزر الماوس الأيمن لنسخ المعرف: {id}" },
  "ach.badgeUnlocking": { en: "Unlocking…", ar: "جاري الفتح…" },
  "ach.unlockedOn": { en: "Unlocked", ar: "فتح في" },
  "ach.xpLabel": { en: "XP", ar: "XP" },
  "ach.ofPlayersUnlock": { en: "of players unlock", ar: "من اللاعبين فتحوا" },

  // DLC tab
  "dlc.title": { en: "DLC Catalog", ar: "كتالوج المحتوى الإضافي" },
  "dlc.entitlements": { en: "Entitlements", ar: "الاستحقاقات" },
  "dlc.noData": { en: "No DLC data available", ar: "لا تتوفر بيانات المحتوى الإضافي" },
  "dlc.dlcsQueried": { en: "DLCs queried", ar: "محتوى إضافي مطلوب" },
  "dlc.owned": { en: "owned", ar: "مملوك" },
  "dlc.catalog": { en: "Catalog", ar: "الكتالوج" },
  "dlc.titles": { en: "titles", ar: "عناوين" },
  "dlc.itemId": { en: "Item ID", ar: "معرف العنصر" },
  "dlc.tableTitle": { en: "Title", ar: "العنوان" },
  "dlc.timesQueried": { en: "Times Queried", ar: "مرات الطلب" },
  "dlc.timesOwned": { en: "Times Owned", ar: "مرات الملك" },
  "dlc.status": { en: "Status", ar: "الحالة" },
  "dlc.emptyConnecting": { en: "Connecting…", ar: "جاري الاتصال…" },
  "dlc.emptyConnectingBody": { en: "Waiting for the Epic Unlocker pipe to deliver the DLC catalog.", ar: "في انتظار أنموذج Epic Unlocker لتسليم كتالوج المحتوى الإضافي." },
  "dlc.emptyNotConnected": { en: "Not connected", ar: "غير متصل" },
  "dlc.emptyNotConnectedBody": { en: "Launch a game with Epic Unlocker injected to establish the pipe connection. The DLC catalog will populate automatically.", ar: "قم بتشغيل لعبة مع Epic Unlocker محقون لتأسيس اتصال الأنموذج. سيتم ملء كتالوج المحتوى الإضافي تلقائياً." },
  "dlc.emptyNoDlc": { en: "No DLC", ar: "لا يوجد محتوى إضافي" },
  "dlc.emptyNoDlcBody": { en: "The pipe is connected but no DLC catalog has been received yet.", ar: "الأنموذج متصل ولكن لم يتم استلام كتالوج المحتوى الإضافي بعد." },

  // Log tab
  "log.title": { en: "ScreamAPI Log", ar: "سجل ScreamAPI" },
  "log.clear": { en: "Clear", ar: "مسح" },
  "log.clearing": { en: "Clearing…", ar: "جاري المسح…" },
  "log.cleared": { en: "Cleared ✓", ar: "تم المسح ✓" },
  "log.openFile": { en: "Open File", ar: "فتح الملف" },
  "log.opening": { en: "Opening…", ar: "جاري الفتح…" },
  "log.autoScroll": { en: "Auto-scroll", ar: "تمرير تلقائي" },
  "log.clearTitle": { en: "Wipe ScreamAPI.log on disk", ar: "مسح ScreamAPI.log على القرص" },
  "log.autoScrollTitle": { en: "Pin the log view to the bottom when new lines arrive", ar: "تثبيت عرض السجل في الأسفل عند وصول سطور جديدة" },
  "log.openFileTitle": { en: "Open ScreamAPI.log in your default .log editor", ar: "فتح ScreamAPI.log في محرر السجلات الافتراضي" },
  "log.filterPlaceholder": { en: "Filter log lines…", ar: "تصفية سطور السجل…" },
  "log.noLines": { en: "No log entries", ar: "لا توجد سجلات" },
  "log.noLogPath": { en: "(no log path yet)", ar: "(لا يوجد مسار سجل بعد)" },
  "log.connecting": { en: "⏳ Connecting to Epic Unlocker pipe…", ar: "⏳ جاري الاتصال بأنموذج Epic Unlocker…" },
  "log.notConnected": { en: "🔌 Not connected. Launch a game with Epic Unlocker injected to start receiving log output.", ar: "🔌 غير متصل. قم بتشغيل لعبة مع Epic Unlocker محقون لبدء استلام مخرجات السجل." },
  "log.waitingForLines": { en: "📄 Log file: {path} — waiting for new log lines…", ar: "📄 ملف السجل: {path} — في انتظار سطور سجل جديدة…" },
  "log.noPathYet": { en: "📄 No log path received yet.", ar: "📄 لم يتم استلام مسار السجل بعد." },
  "log.noFilterMatch": { en: "No log lines match your filter.", ar: "لا توجد سطور سجل تطابق الفلتر." },

  // Settings tab
  "settings.title": { en: "Settings", ar: "الإعدادات" },
  "settings.theme": { en: "Theme", ar: "المظهر" },
  "settings.sdkLogPath": { en: "SDK Log Path", ar: "مسار سجل SDK" },
  "settings.openSdkLog": { en: "Open SDK Log", ar: "فتح سجل SDK" },
  "settings.appearance": { en: "Appearance", ar: "المظهر" },
  "settings.themeLabel": { en: "Theme", ar: "المظهر" },
  "settings.themeDesc": { en: "Choose your preferred color scheme", ar: "اختر نظام الألوان المفضل لديك" },
  "settings.behavior": { en: "Behavior", ar: "السلوك" },
  "settings.autoRefresh": { en: "Auto-refresh interval", ar: "فاصل التحديث التلقائي" },
  "settings.autoRefreshDesc": { en: "How often to poll the game for updates", ar: "كم مرة يتم سؤال اللعبة عن التحديثات" },
  "settings.connectOnLaunch": { en: "Connect on launch", ar: "اتصل عند التشغيل" },
  "settings.connectOnLaunchDesc": { en: "Automatically attempt to connect when the app starts", ar: "محاولة الاتصال تلقائياً عند تشغيل التطبيق" },
  "settings.hotkeys": { en: "Hotkeys", ar: "اختصارات" },
  "settings.hotkeyUnlockAll": { en: "Unlock All", ar: "فتح الكل" },
  "settings.hotkeyUnlockList": { en: "Unlock from List", ar: "فتح من القائمة" },
  "settings.hotkeyLogOverlay": { en: "Toggle Log Overlay", ar: "تبديل غطاء السجل" },
  "settings.logging": { en: "Logging", ar: "التسجيل" },
  "settings.maxLogLines": { en: "Maximum log lines", ar: "الحد الأقصى لسطور السجل" },
  "settings.maxLogLinesDesc": { en: "Number of lines retained in the log view (older lines are dropped)", ar: "عدد السطور المحفوظة في عرض السجل (السطور الأقدم يتم حذفها)" },
  "settings.screamApiLog": { en: "ScreamAPI.log", ar: "ScreamAPI.log" },
  "settings.screamApiLogDesc": { en: "Curated unlock-debugging log with hook events, achievement state, and pipe messages", ar: "سجل منسق لتصحيح الفتح مع أحداث الربط وحالة الإنجازات ورسائل الأنموذج" },
  "settings.sdkLog": { en: "EOS SDK log", ar: "سجل EOS SDK" },
  "settings.sdkLogDesc": { en: "Verbose EOS SDK backend trace (separate from ScreamAPI.log to avoid noise)", ar: "تتبع مفصل لخلفية EOS SDK (منفصل عن ScreamAPI.log لتجنب الضوضاء)" },
  "settings.sdkLogNotAvailable": { en: "Not available until a game connects", ar: "غير متوفر حتى تتصل لعبة" },
  "settings.open": { en: "Open", ar: "فتح" },

  // Statusbar
  "status.connected": { en: "Connected", ar: "متصل" },
  "status.disconnected": { en: "Disconnected", ar: "غير متصل" },
  "status.connecting": { en: "Connecting…", ar: "جاري الاتصال…" },
  "status.logSize": { en: "Log size", ar: "حجم السجل" },

  // Unlock All Modal
  "modal.unlockTitle": { en: "Unlock All Achievements", ar: "فتح جميع الإنجازات" },
  "modal.unlockBody1": { en: "This will attempt to unlock", ar: "سيتم محاولة فتح" },
  "modal.unlockBody2": { en: "all achievements", ar: "جميع الإنجازات" },
  "modal.unlockBody3": { en: "for the current game.", ar: "للعبة الحالية." },
  "modal.statGated": { en: "This includes stat-gated achievements which will be force-ingested via the EOS stats interface.", ar: "يشمل ذلك الإنجازات المرتبطة بإحصائيات والتي سيتم إدخالها قسراً عبر واجهة إحصائيات EOS." },
  "modal.dangerTitle": { en: "DANGER: Account Warning", ar: "خطر: تحذير الحساب" },
  "modal.dangerBody": { en: "Using this on a legitimate Epic Games account may flag your profile on achievement tracking sites like", ar: "استخدام هذا على حساب Epic Games حقيقي قد يضع علامة على ملفك الشخصي في مواقع تتبع الإنجازات مثل" },
  "modal.dangerOr": { en: "or", ar: "أو" },
  "modal.dangerMarked": { en: "as a \"cheater\"", ar: "كـ \"غشاش\"" },
  "modal.dangerBanned": { en: "or banned from leaderboards.", ar: "أو حظر من لوحات المتصدرين." },
  "modal.areYouSure": { en: "Are you sure you want to proceed?", ar: "هل أنت متأكد أنك تريد المتابعة؟" },
  "modal.cancel": { en: "Cancel", ar: "إلغاء" },
  "modal.confirm": { en: "Yes, Unlock All", ar: "نعم، فتح الكل" },

  // Stat-gated tooltip
  "tooltip.statGatedTitle": { en: "Stat-Gated Achievement", ar: "إنجاز مرتبط بإحصائية" },
  "tooltip.statGatedBody": { en: "These achievements unlock when underlying game stats reach required thresholds. The unlocker force-ingests the stat values through the EOS Stats interface. After triggering, please wait 5–30 seconds for the game to process the new stats and grant the achievement.", ar: "تفتح هذه الإنجازات عندما تصل إحصائيات اللعبة الأساسية إلى الحدود المطلوبة. يقوم الفاتح بإدخال قيم الإحصائيات قسراً عبر واجهة إحصائيات EOS. بعد التفعيل، يرجى الانتظار 5–30 ثانية لمعالجة اللعبة للإحصائيات الجديدة ومنح الإنجاز." },

  // Language toggle tooltip
  "lang.toggle": { en: "Switch to العربية", ar: "التبديل إلى English" },

  // Titlebar
  "titlebar.cycleThemes": { en: "Cycle themes (Gold → Light → Dark)", ar: "تبديل المظاهر (ذهبي → فاتح → داكن)" },
  "titlebar.pipeConnected": { en: "Pipe connected", ar: "الأنموذج متصل" },
  "titlebar.pipeNotConnected": { en: "Pipe not connected", ar: "الأنموذج غير متصل" },
  "titlebar.minimize": { en: "Minimize", ar: "تصغير" },
  "titlebar.maximize": { en: "Maximize / Restore", ar: "تكبير / استعادة" },
  "titlebar.close": { en: "Close", ar: "إغلاق" },
  "titlebar.volume": { en: "Volume", ar: "الصوت" },

  // Music
  "music.toggleOn": { en: "Play music", ar: "تشغيل الموسيقى" },
  "music.toggleOff": { en: "Stop music", ar: "إيقاف الموسيقى" },
  "music.mute": { en: "Mute", ar: "كتم" },
  "music.unmute": { en: "Unmute", ar: "إلغاء الكتم" },

  // Toast messages
  "toast.notConnected": { en: "Not connected", ar: "غير متصل" },
  "toast.notConnectedBody": { en: "Launch a game with Epic Unlocker injected first.", ar: "قم بتشغيل لعبة مع Epic Unlocker محقون أولاً." },
  "toast.notConnectedLogBody": { en: "Launch a game first so ScreamAPI reports its log path.", ar: "قم بتشغيل لعبة أولاً حتى يُبلغ ScreamAPI عن مسار السجل." },
  "toast.unlocking": { en: "Unlocking…", ar: "جاري الفتح…" },
  "toast.unlockingBody": { en: "Queued", ar: "تم وضع في الطابور" },
  "toast.queued": { en: "Queued", ar: "في الطابور" },
  "toast.queuedBody": { en: "— wait 5–30s for the game to process.", ar: "— انتظر 5–30 ثانية لمعالجة اللعبة." },
  "toast.failed": { en: "Failed", ar: "فشل" },
  "toast.failedBody": { en: "Could not queue. Check the log for details.", ar: "لم يمكن وضعه في الطابور. تحقق من السجل للتفاصيل." },
  "toast.refreshing": { en: "Refreshing…", ar: "جاري التحديث…" },
  "toast.refreshingBody": { en: "Re-syncing from game", ar: "إعادة المزامنة من اللعبة" },
  "toast.refreshed": { en: "Refreshed", ar: "تم التحديث" },
  "toast.refreshedBody": { en: "Achievement list re-synced.", ar: "تمت إعادة مزامنة قائمة الإنجازات." },
  "toast.fetchIcons": { en: "Fetching icons…", ar: "جاري جلب الأيقونات…" },
  "toast.fetchIconsBody": { en: "Downloading achievement icons to local cache.", ar: "تحميل أيقونات الإنجازات إلى ذاكرة التخزين المؤقت." },
  "toast.iconsFetched": { en: "Icons fetched", ar: "تم جلب الأيقونات" },
  "toast.iconsFetchedFail": { en: "Fetched with failures", ar: "تم الجلب مع فشل" },
  "toast.fetchFailed": { en: "Fetch failed", ar: "فشل الجلب" },
  "toast.fetchRarity": { en: "Fetching rarity…", ar: "جاري جلب الندرة…" },
  "toast.fetchRarityBody": { en: "Querying egdata + Epic GraphQL for achievement rarity.", ar: "الاستعلام عن ندرة الإنجازات من egdata + Epic GraphQL." },
  "toast.rarityFetched": { en: "Rarity fetched", ar: "تم جلب الندرة" },
  "toast.rarityFetchedBody": { en: "achievements now have rarity data (Bronze/Silver/Gold/Platinum).", ar: "الإنجازات لديها الآن بيانات ندرة (برونزي/فضي/ذهبي/بلاتيني)." },
  "toast.noRarity": { en: "No rarity data", ar: "لا توجد بيانات ندرة" },
  "toast.noRarityBody": { en: "No matching achievements found in egdata or Epic GraphQL. The game may not be listed.", ar: "لم يتم العثور على إنجازات مطابقة في egdata أو Epic GraphQL. اللعبة قد لا تكون مدرجة." },
  "toast.rarityFetchFailed": { en: "Rarity fetch failed", ar: "فشل جلب الندرة" },
  "toast.clearingCache": { en: "Clearing icon cache…", ar: "جاري مسح ذاكرة الأيقونات…" },
  "toast.clearingCacheBody": { en: "Deleting cached icon files from disk.", ar: "حذف ملفات الأيقونات المخزنة من القرص." },
  "toast.cacheCleared": { en: "Cache cleared", ar: "تم مسح الذاكرة" },
  "toast.cacheClearedBody": { en: "Deleted cached icons. Run Fetch Icons to re-download.", ar: "تم حذف الأيقونات المخزنة. شغّل جلب الأيقونات لإعادة التحميل." },
  "toast.cacheClearedCount": { en: "Deleted {count} cached icon(s).", ar: "تم حذف {count} أيقونة مخزنة." },
  "toast.iconsDetail": { en: "New: {ok} · Cached: {skipped} · No URL: {noUrl}", ar: "جديد: {ok} · مخزن: {skipped} · بدون رابط: {noUrl}" },
  "toast.iconsDetailFailed": { en: "New: {ok} · Cached: {skipped} · No URL: {noUrl} · Failed: {failed}. See log for details.", ar: "جديد: {ok} · مخزن: {skipped} · بدون رابط: {noUrl} · فشل: {failed}. راجع السجل للتفاصيل." },
  "toast.noCachedIcons": { en: "No cached icons found on disk.", ar: "لا توجد أيقونات مخزنة على القرص." },
  "toast.clearFailed": { en: "Clear failed", ar: "فشل المسح" },
  "toast.unlockAllStarted": { en: "Unlock All Started", ar: "بدأ فتح الكل" },
  "toast.unlockAllBody": { en: "Queued all achievements.", ar: "تم وضع جميع الإنجازات في الطابور." },
  "toast.processing": { en: "Processing", ar: "جاري المعالجة" },
  "toast.processingBody": { en: "Achievements will unlock over 5–30s each as the game processes stats.", ar: "ستفتح الإنجازات خلال 5–30 ثانية لكل إنجاز أثناء معالجة اللعبة للإحصائيات." },
  "toast.unlockAllFailed": { en: "Could not queue unlock-all. Check the log for details.", ar: "لم يمكن وضع فتح-الكل في الطابور. تحقق من السجل للتفاصيل." },
  "toast.logCleared": { en: "Log cleared", ar: "تم مسح السجل" },
  "toast.logClearedBody": { en: "ScreamAPI.log wiped on disk.", ar: "تم مسح ScreamAPI.log على القرص." },
  "toast.couldNotOpenLog": { en: "Could not open log", ar: "لم يمكن فتح السجل" },

  // Manifest sharing consent
  "consent.title": { en: "Help Build the Manifest Database", ar: "ساعد في بناء قاعدة بيانات البيانات" },
  "consent.body": { en: "Help build a public game manifest database for Epic Games by anonymously sharing your game files. No personal data is sent. You can disable this anytime in Settings.", ar: "ساعد في بناء قاعدة بيانات عامة لبيانات ألعاب Epic من خلال مشاركة ملفات ألعابك بشكل مجهول. لا يتم إرسال بيانات شخصية. يمكنك تعطيل هذا في أي وقت من الإعدادات." },
  "consent.accept": { en: "OK, got it", ar: "حسناً، فهمت" },
  "consent.decline": { en: "Disable", ar: "تعطيل" },

  // Manifest sharing settings
  "settings.manifestSharing": { en: "Manifest Sharing", ar: "مشاركة البيانات" },
  "settings.shareManifests": { en: "Share anonymous manifests", ar: "مشاركة البيانات بشكل مجهول" },
  "settings.shareManifestsDesc": { en: "Upload game manifests to the public database. No personal data is included.", ar: "رفع بيانات الألعاب إلى قاعدة البيانات العامة. لا تتضمن بيانات شخصية." },
  "settings.syncManifests": { en: "Sync Now", ar: "مزامنة الآن" },
  "settings.syncManifestsDesc": { en: "Scan and upload any new manifests", ar: "فحص ورفع أي بيانات جديدة" },

  // Manifest toast messages
  "toast.manifestScanning": { en: "Scanning manifests…", ar: "جاري فحص البيانات…" },
  "toast.manifestScanFound": { en: "Found {count} manifest(s)", ar: "تم العثور على {count} بيانات" },
  "toast.manifestUploading": { en: "Uploading manifests…", ar: "جاري رفع البيانات…" },
  "toast.manifestUploadDone": { en: "{count} manifest(s) uploaded", ar: "تم رفع {count} بيانات" },
  "toast.manifestUploadSkipped": { en: "All manifests already synced", ar: "جميع البيانات متزامنة بالفعل" },
  "toast.manifestUploadFailed": { en: "Manifest upload failed", ar: "فشل رفع البيانات" },
  "toast.manifestNoConsent": { en: "Manifest sharing is disabled", ar: "مشاركة البيانات معطلة" },
};

export function t(key: string, locale: Locale): string {
  const entry = strings[key];
  if (!entry) return key;
  return entry[locale] ?? entry.en ?? key;
}

export function isRTL(locale: Locale): boolean {
  return locale === "ar";
}
