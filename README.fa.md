<div dir="rtl">

# WiFiTahoeBridge

**وای‌فای Broadcom (خانواده‌ی BCM4360) و AirDrop روی macOS 26 Tahoe، بدون root patch برنامه‌ی OpenCore Legacy Patcher.**

[English](README.md)

یک پلاگین کوچک [Lilu](https://github.com/acidanthera/Lilu) برای هکینتاش (OpenCore). کاری می‌کند که نرم‌افزار وای‌فای و AirDrop خود Tahoe بتواند با درایور قدیمی Broadcom که OpenCore تزریق می‌کند کار کند. ولوم سیستم دست‌نخورده و sealed می‌ماند: نه `IO80211.framework` و `wifip2pd` عوض می‌شوند و نه بعد از هر به‌روزرسانی باید دوباره پچ کرد.

| | |
|---|---|
| وضعیت | روی سیستم نویسنده کار می‌کند: وای‌فای (اسکن، اتصال، DHCP) و AirDrop در هر دو جهت با آیفون و مک |
| آزمایش‌شده روی | macOS 26.7.3 (25G342)، یک کامپیوتر، وای‌فای BCM4360 و بلوتوث BCM20702 (ASUS BT400) |
| نسخه | 0.3.0 |

## چه مشکلی را حل می‌کند

اپل درایور کارت‌های قدیمی Broadcom (یعنی BCM4360، BCM43602، BCM4350 و BCM4352 با AirportBrcmFixup) را در macOS 14 Sonoma حذف کرد. راه‌حل رایج امروز دو نیمه دارد:

1. **نیمه‌ی کرنل** که OpenCore تزریق می‌کند: `IOSkywalkFamily` (نسخه‌ی قدیمی)، `IO80211FamilyLegacy` و `AirPortBrcmNIC`. این نیمه به ولوم سیستم دست نمی‌زند.
2. **نیمه‌ی فضای کاربر** که همان *root patch* برنامه‌ی OpenCore Legacy Patcher است ("Modern Wireless"): جایگزین کردن `IO80211.framework`، `WiFiPeerToPeer.framework` و `wifip2pd` روی ولوم سیستم با نسخه‌های Ventura.

دردسر اصلی نیمه‌ی دوم است. مُهر ولوم سیستم را می‌شکند، باید SIP را پایین آورد و AMFI را دور زد، و بعد از هر به‌روزرسانی macOS باید دوباره انجام شود.

روی Tahoe بدون root patch، نیمه‌ی کرنل بارگذاری می‌شود و کارت دیده می‌شود، ولی:
- اسکن هیچ شبکه‌ای نشان نمی‌دهد و اتصال ممکن نیست، چون `IO80211.framework` در Tahoe و درایور دوران Ventura سر سه درخواست با هم اختلاف دارند؛
- AirDrop اصلاً راه نمی‌افتد، چون `wifip2pd` در Tahoe یک رابط کرنلی می‌خواهد که درایور قدیمی ندارد (همان «API user client» جدید وای‌فای با صف رویداد) و با خطای `Unable to create AWDLInterface for awdl0 because initialization error: Invalid Parameters` متوقف می‌شود.

WiFiTahoeBridge هر دو کمبود را داخل کرنل پر می‌کند. برای AirDrop یک بخش بلوتوث هم لازم است که کد نمی‌خواهد: بخش [بلوتوث](#بلوتوث-لازم-برای-airdrop) را ببینید.

## چه چیزی کار می‌کند و چه چیزی نه

| قابلیت | وضعیت |
|---|---|
| اسکن و فهرست شبکه‌ها در منوی وای‌فای | کار می‌کند |
| اتصال به شبکه‌ی WPA2/WPA3 Personal، DHCP و ترافیک | کار می‌کند (۵ گیگاهرتز، ۸۰ مگاهرتز، 802.11ac، سرعت لینک ۱۰۵۳ مگابیت) |
| اتصال خودکار بعد از بوت | کار می‌کند |
| AWDL: بالا آمدن `awdl0` و `llw0`، پیدا کردن دستگاه‌ها، مسیر داده | کار می‌کند (با آیفون و یک مک با macOS 27) |
| AirDrop | در هر دو جهت کار می‌کند (آیفون به هکینتاش، هکینتاش به آیفون و مک)، به‌همراه تنظیم بلوتوث پایین |
| ولوم سیستم sealed، بدون root patch | بله |
| Handoff، کپی‌پیست مشترک، AirPlay به مک، Sidecar | هنوز آزمایش نشده |
| اسلیپ و بیدار شدن، کارکرد طولانی | هنوز آزمایش نشده |
| شبکه‌ی فقط WPA3، WPA Enterprise، شبکه‌ی باز یا WEP، هات‌اسپات | آزمایش نشده |
| کارت‌های غیر از BCM4360، بلوتوث غیر از BCM20702، مک‌های واقعی | آزمایش نشده |
| macOS 15 Sequoia و قدیمی‌تر | پشتیبانی نمی‌شود (kext فقط روی Darwin 25 فعال می‌شود) |

فقط روی یک سیستم آزمایش شده است. قبل از امتحان، یک نسخه‌ی پشتیبان قابل بوت از EFI خود (روی فلش) داشته باشید.

## پیش‌نیازها

- OpenCore با نسخه‌ای از [Lilu](https://github.com/acidanthera/Lilu/releases) که از Tahoe پشتیبانی کند (با 1.7.2 آزمایش شده).
- نیمه‌ی کرنل که از قبل برای Tahoe تنظیم شده باشد. این kextها از [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher/tree/main/payloads/Kexts/Wifi) می‌آیند (پوشه‌ی `payloads/Kexts/Wifi`). فایل‌های اپل هستند و در این مخزن نیستند:
  - `IOSkywalkFamily.kext` به‌همراه یک ورودی در `Kernel > Block` برای `com.apple.iokit.IOSkywalkFamily` (با `Strategy` برابر `Exclude` و `MinKernel` برابر `23.0.0`)
  - `IO80211FamilyLegacy.kext`
  - `AirPortBrcmNIC.kext` از بسته‌ی `AirPortBrcmNIC-Tahoe` (با `MinKernel` برابر `25.0.0`)
- کارتی که با `AirPortBrcmNIC` کار کند. آزمایش‌شده: BCM4360 (کارت TP-Link Archer T9E با شناسه‌ی `14e4:43a0`).
- برای AirDrop: بلوتوث سالم و یک Apple Account مشترک روی هر دو دستگاه، مثل یک مک واقعی.

اگر کارت روی Tahoe در System Information > Wi-Fi دیده می‌شود ولی هیچ شبکه‌ای پیدا نمی‌کند، نیمه‌ی کرنل سر جایش است و همین kext کم است.

## نصب

1. فایل `WiFiTahoeBridge-0.3.0-RELEASE.zip` را از [Releases](https://github.com/hamedafra/WiFiTahoeBridge/releases) بگیرید و `WiFiTahoeBridge.kext` را در `EFI/OC/Kexts` بگذارید.
2. آن را در `config.plist` به `Kernel > Add` اضافه کنید، هر جایی بعد از `Lilu.kext` و بعد از سه kext بالا:

<div dir="ltr">

```xml
<dict>
    <key>Arch</key>
    <string>x86_64</string>
    <key>BundlePath</key>
    <string>WiFiTahoeBridge.kext</string>
    <key>Comment</key>
    <string>Tahoe Wi-Fi and AWDL on IO80211FamilyLegacy</string>
    <key>Enabled</key>
    <true/>
    <key>ExecutablePath</key>
    <string>Contents/MacOS/WiFiTahoeBridge</string>
    <key>MaxKernel</key>
    <string></string>
    <key>MinKernel</key>
    <string>25.0.0</string>
    <key>PlistPath</key>
    <string>Contents/Info.plist</string>
</dict>
```

</div>

3. اگر سیستم قبلاً برای وای‌فای root patch شده، اول در OpenCore Legacy Patcher آن را برگردانید (Revert Root Patches). نصب تازه یا ارتقا به Tahoe پچی ندارد.
4. ریستارت کنید و منوی وای‌فای را باز کنید.
5. برای AirDrop، مرحله‌ی [بلوتوث](#بلوتوث-لازم-برای-airdrop) را یک بار انجام دهید.

برای این‌که ببینید بارگذاری شده و `wifip2pd` از آن استفاده می‌کند:

<div dir="ltr">

```bash
kextstat | grep WiFiTahoeBridge
```

```bash
sysctl debug.wtb_awdl debug.wtb_awdl_opens debug.wtb_awdl_events debug.wtb_awdl_drops
```

</div>

کمی بعد از ورود به سیستم، `debug.wtb_awdl_opens` باید ۱ باشد، و وقتی پنجره‌ی AirDrop باز است `debug.wtb_awdl_events` بالا می‌رود.

### آرگومان‌های بوت

| آرگومان | اثر |
|---|---|
| `-wtboff` | خاموش کردن کل kext |
| `-wtbawdloff` | وای‌فای بماند، بخش AWDL و AirDrop خاموش شود |
| `-wtbdbg` | لاگ دیباگ (نسخه‌ی DEBUG لازم است) |
| `-wtbbeta` | بارگذاری روی نسخه‌ی Darwin جدیدتر از 25 |

بخش AWDL در Safe Mode و در نصب‌کننده و Recovery هم خاموش است. در حین کار هم می‌شود با `sudo sysctl debug.wtb_awdl=0` (یا `=1`) و بعد `sudo launchctl kickstart -k system/com.apple.wifip2pd` روشن و خاموشش کرد.

## بلوتوث (لازم برای AirDrop)

AirDrop دستگاه‌ها را با بلوتوث LE پیدا می‌کند و داده را روی AWDL می‌فرستد. هر دستگاه فقط برای مکی AWDL را باز می‌کند که تبلیغ بلوتوثش را شنیده باشد. روی Tahoe با بلوتوث BCM20702 (این‌جا ASUS BT400) مک هیچ‌وقت تبلیغ نمی‌کند: `bluetoothd` موقع راه‌اندازی از کنترلر «تبلیغ چندگانه» را می‌خواهد، کنترلر رد می‌کند (`LE_InitMultipleAdvertising failed with status 152`) و از آن به بعد همه‌ی درخواست‌های تبلیغ با `Stack is not started, updateAdvertisement returning` کنار گذاشته می‌شوند. نتیجه: مک دستگاه‌های دیگر را در پنجره‌ی AirDrop می‌بیند، ولی خودش برای آن‌ها نامرئی است و هر ارسالی بعد از چند ثانیه شکست می‌خورد.

خود `bluetoothd` تنظیمی برای خاموش کردن این قابلیت دارد و نسخه‌ی عمومی macOS هم آن را می‌خواند:

<div dir="ltr">

```bash
sudo sh tools/bt-single-advertising.sh on
```

</div>

که این دستورها را اجرا می‌کند:

<div dir="ltr">

```bash
sudo defaults write /Library/Preferences/com.apple.MobileBluetooth.debug LE -dict SupportsMultipleAdvertising -bool false
```

```bash
sudo defaults write com.apple.MobileBluetooth.debug LE -dict SupportsMultipleAdvertising -bool false
```

```bash
sudo killall bluetoothd
```

</div>

بعد در لاگ `Warning: Overriding Multiple Advertising support - disabled` و `stackDidStart() -- status = OI_OK` دیده می‌شود و تبلیغ شروع می‌شود. این تنظیم یک فایل تنظیمات است: با ریستارت از بین نمی‌رود و به kext ربطی ندارد. `sudo sh tools/bt-single-advertising.sh off` آن را پاک می‌کند. (در آزمایش هر دو محل نوشته شد؛ این‌که `bluetoothd` کدام را می‌خواند جدا بررسی نشده است.)

این بخش ربطی به وای‌فای ندارد و احتمالاً به هر هکینتاشی با بلوتوث ۴ Broadcom روی Tahoe کمک می‌کند. کنترلرهای بلوتوث ۵ که تبلیغ گسترده دارند احتمالاً به آن نیاز ندارند.

## چطور کار می‌کند

### درخواست‌های وای‌فای

`IO80211.framework` در Tahoe با همان ioctlهای Apple80211 دوران Ventura به درایور می‌رسد (`SIOCSA80211` و `SIOCGA80211` روی یک سوکت، با ساختار `apple80211req`). سه درخواست فرق دارند:

| درخواست | Tahoe می‌فرستد | درایور قدیمی می‌خواهد | ترجمه |
|---|---|---|---|
| `APPLE80211_IOC_SCAN_RESULT` (شماره‌ی 11، خواندن) | بافر 0x8e0 بایتی | دقیقاً 0x48c بایت | با 0x48c صدا زده می‌شود و بقیه صفر می‌شود |
| همان درخواست، زمان‌بندی | بلافاصله نتیجه را می‌خواند | تا پایان اسکن `EBUSY` می‌دهد | هر ۲۰۰ میلی‌ثانیه دوباره، تا ۱۵ ثانیه |
| `APPLE80211_IOC_ASSOCIATE` (شماره‌ی 20، نوشتن) | ساختار ۹۰۸ بایتی | ساختار ۵۹۲ بایتی | دو فیلد ۱۶ بیتی ۳۲ بیتی شده و یک دنباله اضافه شده است |

kext تابع `IO80211Interface::performCommand` در `IO80211FamilyLegacy` را هوک می‌کند و فقط همین درخواست‌ها را بازنویسی می‌کند، آن هم فقط وقتی از یک پردازه‌ی کاربر و دقیقاً با اندازه‌های Tahoe بیایند.

### AWDL

`wifip2pd` در Tahoe روی رابط AWDL یک user client نوع 0 باز می‌کند (`IOServiceOpen`)، درخواست‌های Apple80211 را از آن می‌فرستد و رویدادها را از یک صف در حافظه‌ی مشترک می‌گیرد. در مک واقعی این همان `IO80211APIUserClient` در `IO80211Family` جدید است. درایور قدیمی چنین رابطی ندارد و رویدادهای AWDL را فقط به یک رابط قدیمی‌تر می‌دهد که مجوز خاص می‌خواهد و نرم‌افزار Tahoe هیچ‌وقت بازش نمی‌کند.

kext تابع `IO80211VirtualInterface::newUserClient` را هوک می‌کند و برای نوع 0 روی `awdl*`، وقتی درخواست از `wifip2pd` باشد، `IO80211APIUserClient` خودش را می‌سازد (نام کلاس عمداً همان نام اپل است: sandbox در `wifip2pd` فقط اجازه‌ی باز کردن کلاس‌های فهرست‌شده در مجوزش را می‌دهد). `airportd` هم به `awdl0` وصل می‌شود ولی روی روش قدیمی ioctl می‌ماند: اگر این رابط را بگیرد، `awdl0` را کارت وای‌فای دوم حساب می‌کند و اتصال خودکار با «driver not available» متوقف می‌شود. این رابط:
- درخواست‌ها را (دستور 0) بدون تغییر از طریق `ifnet_ioctl()` به درایور قدیمی می‌دهد؛ همه‌ی درخواست‌های AWDL که `wifip2pd` در آزمایش فرستاد همان‌طور پذیرفته شدند، جز یک درخواست جدید (575) که پشتیبانی نمی‌شود؛
- به پرسش‌های قابلیت (دستورهای 1، 5، 7) جواب می‌دهد و فیلتر رویداد (درخواست 0x234) را نگه می‌دارد؛
- یک صف رویداد با قالب جدید نگه می‌دارد (سرآیند ۰x30 بایتی برای هر رکورد، نشانه‌ی 0x57، پرکننده‌ی 0x50، اندیس خواندن و نوشتن در یک صفحه‌ی مشترک دیگر) و رسیدن داده را با یک async completion خبر می‌دهد؛
- این صف را از هوک `IO80211VirtualInterface::IO80211InterfacePostMessage` پر می‌کند، که همه‌ی رویدادهای AWDL درایور قدیمی از آن رد می‌شوند.

جزئیات، از جمله شماره‌ی درخواست‌ها و قالب صف، در [docs/awdl.md](docs/awdl.md) است؛ ساختار درخواست‌های وای‌فای در [docs/protocol.md](docs/protocol.md).

## چطور پیدا شد: اول آزمایش در فضای کاربر

بیشتر کار، قبل از نوشتن کد کرنل، روی همان Tahoe در حال اجرا و در فضای کاربر اندازه‌گیری و ثابت شد. ابزارها در پوشه‌ی [tools/](tools/) هستند: ثبت‌کننده‌ی ioctl (`tap.c`)، یک مدل فضای کاربر از کل رابط AWDL (`iouc.c`) که باعث شد کتابخانه‌ی واقعی Tahoe و خود `wifip2pd` بدون kext رابط AWDL را بسازند، شنونده‌ی رویدادهای کرنل و چند ابزار کوچک دیگر. راهنما در [tools/README.md](tools/README.md) است.

## ساخت از سورس

<div dir="ltr">

```bash
git clone https://github.com/hamedafra/WiFiTahoeBridge
cd WiFiTahoeBridge
git clone --depth 1 https://github.com/acidanthera/MacKernelSDK
# put a DEBUG build of Lilu.kext (from the Lilu release zip) in this folder, then:
xcodebuild -project WiFiTahoeBridge.xcodeproj -configuration Release -arch x86_64 SYMROOT=build
```

</div>

خروجی در `build/Release/WiFiTahoeBridge.kext` است. با Xcode 27.2 و هدرهای Lilu 1.7.3 ساخته شده.

یک نکته برای کسی که پلاگین Lilu را از روی یک پلاگین دیگر شروع می‌کند: اگر `OSBundleCompatibleVersion` در `Info.plist` از نسخه‌ی خود kext بزرگ‌تر باشد، OpenCore تزریق را موفق گزارش می‌کند و کرنل بی‌صدا kext را کنار می‌گذارد.

## رفع اشکال

- **kext در `kextstat` نیست:** ورودی `Kernel > Add` را بررسی کنید و این‌که Lilu بارگذاری شده و قبل از آن آمده باشد.
- **کارت اصلاً دیده نمی‌شود:** نیمه‌ی کرنل نیست یا مسدود شده. این kext برای آن کاری نمی‌کند.
- **شبکه‌ها می‌آیند ولی اتصال نمی‌شود:** یک issue باز کنید و نسخه‌ی macOS، مدل کارت، نوع امنیت شبکه و در صورت امکان یک tap از ابزارهای `tools/` را بنویسید.
- **AirDrop: دستگاه‌های دیگر دیده می‌شوند ولی ارسال شکست می‌خورد و مک روی آیفون دیده نمی‌شود:** تبلیغ بلوتوث؛ بخش [بلوتوث](#بلوتوث-لازم-برای-airdrop) را ببینید.
- **AirDrop: هیچ دستگاهی دیده نمی‌شود:** `debug.wtb_awdl_opens` را ببینید (باید ۱ باشد) و `log show --last 5m --predicate 'process == "wifip2pd"' | grep AWDLInterface`.
- **مشکل در بوت:** یک بار با `-wtbawdloff` و بعد با `-wtboff` بوت کنید.

## قدردانی

- [acidanthera](https://github.com/acidanthera) برای Lilu و MacKernelSDK.
- [AirportBrcmFixup](https://github.com/acidanthera/AirportBrcmFixup) از lvs1974: این پروژه از اسکلت همان پلاگین شروع شد.
- [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher) و مشارکت‌کنندگانش که این کارت‌ها را روی macOS جدید زنده نگه داشته‌اند و kextهای سمت کرنل را نگهداری می‌کنند.
- [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm) و [ax210-airdrop-awdl-hackintosh](https://github.com/kevineduardo/ax210-airdrop-awdl-hackintosh) که یادداشت‌هایشان درباره‌ی شماره‌ی درخواست‌های AWDL و تبلیغ بلوتوث برای AirDrop مرجع مفیدی بود (کدی از آن‌ها برداشته نشده).

## مجوز

BSD 3-Clause، فایل [LICENSE.txt](LICENSE.txt).

</div>
