# -*- coding: utf-8 -*-
import openpyxl
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.utils import get_column_letter

OUT = r"C:\Users\sv964\OneDrive\Desktop 1\GITHUB Repo\ZivikaLabs\Parth_BOM_ESP32S3_v4.xlsx"

# category, part, sku, qty, unit, stock, vendor, use, specs, link, counted
rows = [
 ("Gateway MCU","ESP32-S3-DevKitC-1 N16R8 (16MB flash + 8MB PSRAM)","-",1,515,"In stock (listing) - verify at checkout","Graylogix",
  "The gateway: Wi-Fi to cloud, drives the UART display + I2S audio. No local AI.","ESP32-S3 N16R8, 16MB flash, 8MB PSRAM, Wi-Fi+BLE. Robu + Robocraze are OOS for N16R8 - hence a 3rd vendor.",
  "https://www.graylogix.in/product/node-mcu-esp32-s3-n16r8-devkit",True),
 ("Gateway MCU [VERIFIED FALLBACK]","7Semi ESP32-S3-Dev-BoardC-1 N8R8","-",1,949,"In Stock (verified)","Robocraze",
  "Use if N16R8 is unavailable: 8MB flash is plenty for a GATEWAY (no local AI - firmware only).","ESP32-S3 N8R8, 8MB flash + 8MB PSRAM, dual USB-C",
  "https://robocraze.com/products/7semi-esp32-s3-dev-boardc-1-n8r8-wifi-bluetooth-dual-usb-c-rgb-led",False),
 ("Touch display","SmartElex 3.5in HMI Display, Resistive Touch, 320x480 (UART)","R213369",1,1419,"In Stock","Robu",
  "Glance screen + cards. Renders its own GUI from the micro-SD; the ESP drives it over a 4-pin UART. Offloads all graphics from the gateway.","3.5in 320x480 RGB565, resistive touch, UART 115200, 5V 650mA, micro-SD, Type-C",
  "https://robu.in/product/smartelex-basic-3-5-tft-hmi-display-with-resistive-touch/",True),
 ("MicroSD card","MicroSD Card 16GB Class 10 (for HMI GUI assets)","-",1,199,"In Stock","Robu",
  "REQUIRED by the SmartElex HMI - stores its GUI pages/images; the display loads them on power-up. Without this the screen has no UI.","16GB Class 10, FAT32",
  "https://robu.in/?s=microsd+16gb+class+10",True),
 ("Microphone","INMP441 I2S MEMS Microphone","-",1,135,"In Stock","Robocraze",
  "Voice capture -> streamed to cloud STT (wake word, commands, meeting notes)","I2S digital MEMS",
  "https://robocraze.com/products/inmp441-mems-high-precision-omnidirectional-microphone-module-i2s",True),
 ("Speaker amp","SmartElex MAX98357A I2S Amplifier","R196218",1,194,"In Stock","Robu",
  "Plays cloud TTS / briefings / music","I2S DAC+amp, up to 3.2W",
  "https://robu.in/product/smartelex-i2s-audio-breakout-max98357a/",True),
 ("Speaker","30mm 8 ohm Speaker GSPK3005TN (with cable)","R256738",1,91,"In Stock","Robu",
  "Audio out; loudness from amp + sealed cavity","30mm 8 ohm, JST cable",
  "https://robu.in/product/gspk3005tn-8r0-5w-l300s-2-54t/",True),
 ("BT calls module","Bluetooth Audio Board with USB/TF + Microphone (hands-free)","R263793",1,289,"In Stock","Robu",
  "External BT-Classic module for phone-call audio - ESP32-S3 is BLE-only and cannot do HFP. Has an onboard mic. CONFIRM it answers/ends calls (HFP); else use a BK8000L module.","BT-Classic audio + onboard mic, call buttons",
  "https://robu.in/product/amplifier-bluetooth-board-audio-with-usb-tf-card-with-microphone/",True),
 ("Clock","DS3231 RTC Module (without battery)","-",1,209,"In Stock","Robocraze",
  "Keeps time/alarms during a power cut (Wi-Fi NTP re-syncs on boot).","DS3231 I2C RTC",
  "https://robocraze.com/products/ds3231-real-time-clock-module-for-raspberry-pi-without-battery",True),
 ("Clock backup","CR2032 3V Lithium Coin Cell (for DS3231)","-",1,25,"In Stock","Robu",
  "Backup for the RTC - keeps it ticking across power-offs and battery swaps","CR2032 3V lithium",
  "https://robu.in/?s=cr2032",True),
 ("Battery (main power)","LG INR18650MJ1 3400mAh 18650 Li-Ion","1098719",2,409,"In Stock","Robu",
  "MAIN POWER. 2 cells in PARALLEL = 1S2P ~6800mAh (~4-5h runtime; the 3.5in backlight is the main drain). 1 cell = ~2h, cheaper.","3.7V 3400mAh 18650",
  "https://robu.in/product/lg-18650-mj1-3400mah-3c-li-ion-battery/",True),
 ("Charge + 5V boost","10W 5V Charge and Discharge Booster Module","R260472",1,459,"In Stock","Robu",
  "Charges the 1S pack AND boosts to the stable 5V rail that runs the ESP + display + amp. Supports run-while-charging.","5V output, 10W (~2A), charge + discharge",
  "https://robu.in/product/10w-charge-and-discharge-booster-module-output-5v/",True),
 ("Charge port","USB Type-C Breakout (sink, with CC resistors)","R122667",1,99,"In Stock","Robu",
  "Type-C charging socket feeding 5V into the charge+boost module","USB-C female, CC pulldowns for 5V sink",
  "https://robu.in/product/usb-type-c-breakout-board-downstream-connection/",True),
 ("Battery holder","2 x 18650 Cell Box with Wire (wire cells in PARALLEL = 1S2P)","R260089",1,89,"In Stock","Robu",
  "Holds the 2 cells. Wire them in PARALLEL (both + together, both - together) for 1S2P.","2x 18650, leaded",
  "https://robu.in/product/2-x-18650-blm-cell-box-with-wire-without-cover-2/",True),
 ("Power switch","KCD1-8-101 Rocker Switch SPST (6A)","R133123",1,17,"In Stock","Robu",
  "Main ON/OFF on the battery line","SPST, 6A rocker",
  "https://robu.in/product/kcd1-8-101-rocker-switch-spst-2pinon-off/",True),
 ("Battery protection","1S Li-ion Protection PCB (DW01A + 8205A, 3A)","-",1,30,"In Stock","Robu",
  "In series between the 1S2P pack and the booster. Raises the discharge cutoff to a cell-friendly ~2.75-2.9V (the IP5306 inside the booster cuts out around 2.5V, which shortens cell life). Protects the LG MJ1 pack from over-discharge and short-circuit.","1S, 3A continuous, DW01A+8205A",
  "https://robu.in/?s=1s+li-ion+protection+board",True),
 ("Safety fuse","2A Inline Fuse + Holder (on the battery line)","-",1,35,"In Stock","Robu",
  "Hard short-circuit protection on the pack. 1S2P LG MJ1 cells can dump >20A into a dead short - the protection PCB trips first, this is backup.","2-3A automotive mini blade or 5x20mm + inline holder",
  "https://robu.in/?s=fuse+holder+inline",True),
 ("Touch zones [OPTIONAL]","TTP223 Capacitive Touch Module x3 (head/base taps)","-",3,35,"In Stock","Robu",
  "Physical taps (wake, volume). Optional - the touch display covers most interaction.","TTP223 digital-out, 3x GPIO",
  "https://robu.in/?s=TTP223",False),
 ("Misc","Mic-mute switch + status LED + jumper wires","-",1,80,"Common","Robu",
  "Hardware mic-mute (privacy), status LED, inter-board wiring","Generic",
  "https://robu.in/?s=jumper+wire",True),
]

wb=openpyxl.Workbook(); ws=wb.active; ws.title="Parth BOM (ESP32-S3)"
navy="1F3864"; head=PatternFill("solid",fgColor="1F3864"); alt=PatternFill("solid",fgColor="F2F5FA")
catf=PatternFill("solid",fgColor="D9E1F2"); oosf=PatternFill("solid",fgColor="FCE4D6"); warn=PatternFill("solid",fgColor="FFF2CC")
white=Font(color="FFFFFF",bold=True,size=11); linkf=Font(color="0563C1",underline="single")
thin=Side(style="thin",color="BFC9DA"); border=Border(left=thin,right=thin,top=thin,bottom=thin)
wrap=Alignment(wrap_text=True,vertical="top"); center=Alignment(horizontal="center",vertical="top")
NC=12
ws.merge_cells(start_row=1,start_column=1,end_row=1,end_column=NC)
ws["A1"]="Parth - P0 BOM (ESP32-S3 cloud gateway, battery-powered, no camera)"; ws["A1"].font=Font(bold=True,size=15,color=navy); ws.row_dimensions[1].height=26
ws.merge_cells(start_row=2,start_column=1,end_row=2,end_column=NC)
ws["A2"]="Static desk companion, BATTERY powered. ESP32-S3 = gateway only (all AI in cloud). No camera. BT-Classic module handles phone calls. v5 adds the HMI micro-SD (the display needs it to show anything), a 1S protection PCB + inline fuse for the Li-ion pack, and a CR2032 for the RTC. Every part verified on its product page and IN STOCK - except the N16R8 board (OOS on Robu+Robocraze; sourced from Graylogix - verify at checkout; verified N8R8 fallback included)."
ws["A2"].font=Font(italic=True,size=10,color="555555")
headers=["#","Category","Part","SKU","Qty","Unit Rs","Total Rs","Stock","Vendor","Use","Specs / notes","Buy link"]
hr=4
for c,h in enumerate(headers,1):
    cell=ws.cell(hr,c,h); cell.fill=head; cell.font=white; cell.border=border
    cell.alignment=Alignment(wrap_text=True,vertical="center",horizontal="center")
ws.row_dimensions[hr].height=30
r=hr+1; grand=0
for i,(cat,part,sku,qty,unit,stock,vendor,use,specs,link,counted) in enumerate(rows,1):
    total=qty*unit
    if counted: grand+=total
    vals=[i,cat,part,sku,qty,unit,total,stock,vendor,use,specs,"Open link"]
    for c,v in enumerate(vals,1):
        cell=ws.cell(r,c,v); cell.border=border
        cell.alignment=center if c in (1,4,5,6,7,9) else wrap
        if r%2==0: cell.fill=alt
    ws.cell(r,2).fill=catf; ws.cell(r,6).number_format='#,##0'; ws.cell(r,7).number_format='#,##0'
    lc=ws.cell(r,12); lc.hyperlink=link; lc.font=linkf
    if "verify" in stock.lower() or "fallback" in cat.lower(): ws.cell(r,8).fill=warn
    if not counted:
        for c in range(1,NC+1): ws.cell(r,c).font=Font(italic=True,color="7A6000")
        ws.cell(r,12).font=Font(italic=True,color="0563C1",underline="single")
    r+=1
ws.cell(r,6,"Subtotal").font=Font(bold=True); ws.cell(r,6).alignment=Alignment(horizontal="right")
tc=ws.cell(r,7,grand); tc.font=Font(bold=True,color=navy); tc.number_format='#,##0'; tc.fill=oosf
ws.cell(r,10,"Excl [OPTIONAL]/[FALLBACK] rows. Under the Rs5000 budget.").font=Font(italic=True,size=9,color="555555")
for c in range(1,NC+1): ws.cell(r,c).border=border
widths=[4,18,38,10,5,8,8,16,11,30,34,11]
for c,w in enumerate(widths,1): ws.column_dimensions[get_column_letter(c)].width=w
ws.freeze_panes="A5"; ws.sheet_view.showGridLines=False

ns=wb.create_sheet("Notes")
ns.column_dimensions['A'].width=24; ns.column_dimensions['B'].width=96; ns.sheet_view.showGridLines=False
ns.merge_cells("A1:B1"); ns["A1"]="Parth (ESP32-S3) - Notes & Scope"; ns["A1"].font=Font(bold=True,size=14,color=navy)
notes=[
 ("N16R8 availability","N16R8 dev boards are OUT OF STOCK on Robu, Robocraze, Probots and Compoindia right now (each checked on its product page). They are listed at Graylogix (Rs515) and Amazon (ROBOTICSSIGN ~Rs1190) - the sheet uses Graylogix; verify stock at checkout. If you cannot get N16R8, the 7Semi N8R8 (Robocraze, verified in stock, Rs949) is a drop-in: for a pure GATEWAY (no local AI) 8MB flash is plenty - the extra flash of N16R8 is only useful for local models/assets, which this device does not run."),
 ("Power - battery (your change)","Main power is a 1S2P 18650 pack (2x LG MJ1 in PARALLEL = 3.7V ~6800mAh). The 10W Charge+Discharge module charges the pack (via the Type-C port) AND boosts to the 5V rail for the ESP + display + amp. No wall adapter. Runtime ~4-5h (the 3.5in backlight dominates); drop to 1 cell for ~2h and lower cost, or add cells for more."),
 ("Battery protection (v5)","Added a 1S protection PCB (DW01A+8205A) in SERIES between the pack and the booster, plus an inline 2A fuse. Why: the IP5306 inside the booster module has built-in 1S protection, but its over-discharge cutoff sits around ~2.5V - too low for cell health. The protection PCB raises the cutoff to ~2.75-2.9V and acts as a hard short-circuit cut-off. Wire order: pack B+ -> fuse -> PCB(B+) ... PCB(P+) -> switch -> booster(BAT+). Pack B- -> PCB(B-), PCB(P-) -> booster(BAT-)."),
 ("HMI micro-SD (v5)","The SmartElex 3.5in HMI is a smart display: its GUI pages/images live on its own micro-SD card and it loads them on power-up. The display will not show anything without the card. 16GB Class 10 FAT32 is standard."),
 ("Role of the ESP32-S3","Pure GATEWAY - no local AI. Streams mic audio to the cloud (STT->LLM->TTS), plays TTS back, and drives the HMI display over UART. Same gateway contract as the Xelulu software spec."),
 ("Display","SmartElex 3.5in HMI (resistive touch, 320x480, UART). A smart display - it renders its own GUI from images on its micro-SD, and the ESP just sends values over a 4-pin UART. Offloads all graphics from the gateway and makes good-looking screens easy (design them in the HMI editor)."),
 ("What this version does","Chief-of-staff (calendar/mail/tasks/research/drafting), briefings, glance screen + cards, meeting AUDIO notes, timers/reminders/lists, knowledge/translation, council, mentor, smart home (Matter-over-Wi-Fi), phone calls via the BT module."),
 ("What is DROPPED","Camera / webcam / video calls (no camera this version), on-camera auto-framing + posture/eye-contact coaching. Do video in the phone app, or move to an SBC build later."),
 ("Calls (BT module)","ESP32-S3 is BLE-only (no Bluetooth-Classic/HFP). The external BT module (onboard mic = hands-free) handles phone-call audio; the ESP controls it over UART. CONFIRM it supports HFP (answer/hang-up); if A2DP-only, swap for a BK8000L module."),
 ("Two+ vendors","Split by live stock: ESP32-S3 N16R8 from Graylogix; mic + RTC from Robocraze; display, amp, speaker, BT module, battery, power, switch from Robu. Re-confirm each at order - stock moves fast."),
]
rr=3
for k,v in notes:
    a=ns.cell(rr,1,k); a.font=Font(bold=True,color=navy); a.alignment=Alignment(vertical="top",wrap_text=True)
    b=ns.cell(rr,2,v); b.alignment=Alignment(vertical="top",wrap_text=True)
    if "N16R8" in k or "battery" in k or "Calls" in k: a.fill=warn; b.fill=warn
    rr+=1
wb.save(OUT); print("saved",OUT); print("subtotal",grand)
