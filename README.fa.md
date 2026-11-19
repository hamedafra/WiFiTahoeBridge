<div dir="rtl">

# WiFiTahoeBridge

**وای‌فای Broadcom (خانواده‌ی BCM4360) روی macOS 26 Tahoe، بدون root patch برنامه‌ی OpenCore Legacy Patcher.**

[English](README.md)

یک پلاگین کوچک [Lilu](https://github.com/acidanthera/Lilu) برای هکینتاش (OpenCore). کاری می‌کند که نرم‌افزار وای‌فای خود Tahoe بتواند با درایور قدیمی Broadcom که OpenCore تزریق می‌کند حرف بزند. ولوم سیستم دست‌نخورده و sealed می‌ماند: نه `IO80211.framework` عوض می‌شود و نه بعد از هر به‌روزرسانی باید دوباره پچ کرد.

| | |
|---|---|
| وضعیت | روی سیستم نویسنده کار می‌کند: اسکن، اتصال، DHCP و استفاده‌ی عادی از منوی وای‌فای |
| آزمایش‌شده روی | macOS 26.7.3 (25G342)، یک کامپیوتر، یک کارت (BCM4360) |
| نسخه | 0.2.0، اولین انتشار عمومی |

## چه مشکلی را حل می‌کند

اپل درایور کارت‌های قدیمی Broadcom (یعنی BCM4360، BCM43602، BCM4350 و BCM4352 با AirportBrcmFixup) را در macOS 14 Sonoma حذف کرد. راه‌حل رایج امروز دو نیمه دارد:

1. **نیمه‌ی کرنل** که OpenCore تزریق می‌کند: `IOSkywalkFamily` (نسخه‌ی قدیمی)، `IO80211FamilyLegacy` و `AirPortBrcmNIC`. این نیمه به ولوم سیستم دست نمی‌زند.
2. **نیمه‌ی فضای کاربر** که همان *root patch* برنامه‌ی OpenCore Legacy Patcher است ("Modern Wireless"): جایگزین کردن `IO80211.framework`، `WiFiPeerToPeer.framework` و `wifip2pd` روی ولوم سیستم با نسخه‌های Ventura.

دردسر اصلی نیمه‌ی دوم است. مُهر ولوم سیستم را می‌شکند، باید SIP را پایین آورد و AMFI را دور زد، و بعد از هر به‌روزرسانی macOS باید دوباره انجام شود.

روی Tahoe بدون root patch، نیمه‌ی کرنل بارگذاری می‌شود و کارت دیده می‌شود (`en1`، نسخه‌ی firmware، وضعیت "Not Associated")، ولی اسکن هیچ شبکه‌ای نشان نمی‌دهد و اتصال ممکن نیست. دلیلش کوچک است: `IO80211.framework` در Tahoe و درایور دوران Ventura سر سه درخواست با هم اختلاف دارند. WiFiTahoeBridge همین سه درخواست را داخل کرنل ترجمه می‌کند و برای خود وای‌فای دیگر root patch لازم نیست.

## چه چیزی کار می‌کند و چه چیزی نه

| قابلیت | وضعیت |
|---|---|
| اسکن و فهرست شبکه‌ها در منوی وای‌فای | کار می‌کند |
| اتصال به شبکه‌ی WPA2/WPA3 Personal، DHCP و ترافیک | کار می‌کند (۵ گیگاهرتز، ۸۰ مگاهرتز، 802.11ac، سرعت لینک ۱۰۵۳ مگابیت) |
| ولوم سیستم sealed، بدون root patch | بله |
| AirDrop، Handoff، AirPlay به مک و بقیه‌ی قابلیت‌های AWDL | **پوشش داده نشده.** این‌ها از `wifip2pd` می‌گذرند که این kext به آن دست نمی‌زند. انتظار کار کردن نداشته باشید |
| شبکه‌ی فقط WPA3، WPA Enterprise، شبکه‌ی باز یا WEP، هات‌اسپات | آزمایش نشده |
| اسلیپ و بیدار شدن، کارکرد طولانی | هنوز آزمایش نشده |
| کارت‌های غیر از BCM4360 و مک‌های واقعی | آزمایش نشده |
| macOS 15 Sequoia و قدیمی‌تر | پشتیبانی نمی‌شود (kext فقط روی Darwin 25 فعال می‌شود) |

این اولین انتشار است و فقط روی یک سیستم آزمایش شده. قبل از امتحان، یک نسخه‌ی پشتیبان قابل بوت از EFI خود (روی فلش) داشته باشید.

## پیش‌نیازها

- OpenCore با نسخه‌ای از [Lilu](https://github.com/acidanthera/Lilu/releases) که از Tahoe پشتیبانی کند (با 1.7.2 آزمایش شده).
- نیمه‌ی کرنل که از قبل برای Tahoe تنظیم شده باشد. این kextها از [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher/tree/main/payloads/Kexts/Wifi) می‌آیند (پوشه‌ی `payloads/Kexts/Wifi`). فایل‌های اپل هستند و در این مخزن نیستند:
  - `IOSkywalkFamily.kext` به‌همراه یک ورودی در `Kernel > Block` برای `com.apple.iokit.IOSkywalkFamily` (با `Strategy` برابر `Exclude` و `MinKernel` برابر `23.0.0`)
  - `IO80211FamilyLegacy.kext`
  - `AirPortBrcmNIC.kext` از بسته‌ی `AirPortBrcmNIC-Tahoe` (با `MinKernel` برابر `25.0.0`)
- کارتی که با `AirPortBrcmNIC` کار کند. آزمایش‌شده: BCM4360 (کارت TP-Link Archer T9E با شناسه‌ی `14e4:43a0`).

اگر کارت روی Tahoe در System Information > Wi-Fi دیده می‌شود ولی هیچ شبکه‌ای پیدا نمی‌کند، نیمه‌ی کرنل سر جایش است و همین kext کم است.

## نصب

1. فایل `WiFiTahoeBridge-0.2.0-RELEASE.zip` را از [Releases](https://github.com/hamedafra/WiFiTahoeBridge/releases) بگیرید و `WiFiTahoeBridge.kext` را در `EFI/OC/Kexts` بگذارید.
2. آن را در `config.plist` به `Kernel > Add` اضافه کنید، هر جایی بعد از `Lilu.kext`:

<div dir="ltr">

```xml
<dict>
    <key>Arch</key>
    <string>x86_64</string>
    <key>BundlePath</key>
    <string>WiFiTahoeBridge.kext</string>
    <key>Comment</key>
    <string>Tahoe Wi-Fi requests to IO80211FamilyLegacy</string>
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

برای این‌که ببینید بارگذاری شده:

<div dir="ltr">

```bash
kextstat | grep WiFiTahoeBridge
```

</div>

### آرگومان‌های بوت

| آرگومان | اثر |
|---|---|
| `-wtboff` | خاموش کردن kext |
| `-wtbdbg` | لاگ دیباگ (نسخه‌ی DEBUG لازم است) |
| `-wtbbeta` | بارگذاری روی نسخه‌ی Darwin جدیدتر از 25 |

### پیکربندی‌ای که با آن آزمایش شد

مادربرد Asus TUF Z390-Pro Gaming، پردازنده‌ی i9-9900K، SMBIOS از نوع iMac20,1، OpenCore 1.0.8، Lilu 1.7.2، AirportBrcmFixup 2.2.1، IOSkywalkFamily 1.0، IO80211FamilyLegacy 1200.12.2b1 و AirPortBrcmNIC 1400.1.1.

این سیستم از یک Sequoia پچ‌شده ارتقا پیدا کرده و تنظیمات قدیمی‌اش هنوز هست: `AMFIPass.kext`، مقدار `csr-active-config` برابر `03080000`، مقدار `SecureBootModel` برابر `Disabled` و آرگومان بوت `-lilubetaall`. این‌که kext با SIP کاملاً روشن و Secure Boot پیش‌فرض هم کار می‌کند یا نه **بررسی نشده** است. اگر امتحان کردید، نتیجه را گزارش کنید.

## چطور کار می‌کند

`IO80211.framework` در Tahoe با همان ioctlهای Apple80211 دوران Ventura به درایور می‌رسد (`SIOCSA80211` و `SIOCGA80211` روی یک سوکت، با ساختار `apple80211req`). سه درخواست فرق دارند:

| درخواست | Tahoe می‌فرستد | درایور قدیمی می‌خواهد | ترجمه |
|---|---|---|---|
| `APPLE80211_IOC_SCAN_RESULT` (شماره‌ی 11، خواندن) | بافر 0x8e0 بایتی | دقیقاً 0x48c بایت | با 0x48c صدا زده می‌شود و بقیه صفر می‌شود. سرآیند یکی است؛ انتها فقط جای بیشتری برای IE و یک فیلد تازه دارد |
| همان درخواست، زمان‌بندی | بلافاصله نتیجه را می‌خواند | تا پایان اسکن `EBUSY` می‌دهد | هر ۲۰۰ میلی‌ثانیه دوباره، تا ۱۵ ثانیه |
| `APPLE80211_IOC_ASSOCIATE` (شماره‌ی 20، نوشتن) | ساختار ۹۰۸ بایتی | ساختار ۵۹۲ بایتی | دو فیلد ۱۶ بیتی ۳۲ بیتی شده و یک دنباله اضافه شده است |

`APPLE80211_IOC_SCAN_REQ` (شماره‌ی 10) هم در Tahoe بزرگ‌تر است (0x1598 بایت)، ولی درایور قدیمی همان را قبول می‌کند.

kext تابع `IO80211Interface::performCommand` در `IO80211FamilyLegacy` را هوک می‌کند و فقط همین درخواست‌ها را بازنویسی می‌کند، آن هم فقط وقتی از یک پردازه‌ی کاربر و دقیقاً با اندازه‌های Tahoe بیایند. بقیه بدون تغییر رد می‌شوند. جزئیات بیشتر در [docs/protocol.md](docs/protocol.md) است.

## چطور پیدا شد: آزمایش در فضای کاربر، بدون ریستارت

همه‌چیز اول در فضای کاربر و روی همان Tahoe در حال اجرا اندازه‌گیری و ثابت شد، بدون sudo و بدون ریستارت. ابزارها در پوشه‌ی [tools/](tools/) هستند:

- `tap.c`: با `DYLD_INSERT_LIBRARIES` سوار یک پردازه می‌شود و همه‌ی ioctlهای Apple80211 آن را (بایت‌های درخواست و جواب) ثبت می‌کند. با `TAP_SHIM=1` همان ترجمه‌های kext را در فضای کاربر انجام می‌دهد.
- `wl.c`: یک کتابخانه‌ی IO80211 دلخواه را بار می‌کند (نسخه‌ی اصلی Tahoe یا `IO80211Old.dylib` از Ventura) و `Apple80211Scan` و در صورت نیاز `Apple80211Associate` را با یک رمز عمداً غلط صدا می‌زند.
- `raw.c`: درخواست خام اسکن و نتیجه‌ی اسکن با اندازه‌ی بافر دلخواه.
- `scan.swift`: اسکن با CoreWLAN.

اجرای `wl` با هر دو کتابخانه و مقایسه‌ی خروجی‌ها تفاوت‌ها را نشان می‌دهد. با روشن بودن shim، کتابخانه‌ی اصلی Tahoe همان شبکه‌های Ventura را برگرداند و درخواست اتصالش بایت به بایت با درخواست Ventura یکی شد. kext بعد از آن نوشته شد. اگر درخواست دیگری هم ترجمه لازم داشته باشد، همین ابزارها پیدایش می‌کنند. راهنما در [tools/README.md](tools/README.md) است.

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

یک نکته برای کسی که پلاگین Lilu را از روی یک پلاگین دیگر شروع می‌کند: اگر `OSBundleCompatibleVersion` در `Info.plist` از نسخه‌ی خود kext بزرگ‌تر باشد، OpenCore تزریق را موفق گزارش می‌کند و کرنل بی‌صدا kext را کنار می‌گذارد. نسخه‌ی 0.1 این پروژه چند ریستارت را سر همین از دست داد.

## رفع اشکال

- **kext در `kextstat` نیست:** ورودی `Kernel > Add` را بررسی کنید و این‌که Lilu بارگذاری شده و قبل از آن آمده باشد.
- **کارت اصلاً دیده نمی‌شود:** نیمه‌ی کرنل نیست یا مسدود شده. این kext برای آن کاری نمی‌کند.
- **شبکه‌ها می‌آیند ولی اتصال نمی‌شود:** یک issue باز کنید و نسخه‌ی macOS، مدل کارت، نوع امنیت شبکه و در صورت امکان یک tap از ابزارهای `tools/` را بنویسید.
- **مشکل در بوت:** یک بار با `-wtboff` بوت کنید.

## قدردانی

- [acidanthera](https://github.com/acidanthera) برای Lilu و MacKernelSDK.
- [AirportBrcmFixup](https://github.com/acidanthera/AirportBrcmFixup) از lvs1974: این پروژه از اسکلت همان پلاگین شروع شد.
- [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher) و مشارکت‌کنندگانش که این کارت‌ها را روی macOS جدید زنده نگه داشته‌اند و kextهای سمت کرنل را نگهداری می‌کنند.

## مجوز

BSD 3-Clause، فایل [LICENSE.txt](LICENSE.txt).

</div>
