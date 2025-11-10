# T500RS Windows Capture Analysis
**Generated:** ven. 17 oct. 2025 10:07:42 CEST

---

# Part 1: Device Initialization


## device_init_dedup

```
Frame | USB Data
------|----------
```

**Decoded Commands:**


## device_init

```
Frame | USB Data
------|----------
```

**Decoded Commands:**


---

# Part 2: Constant Force Test


## device_const_force_pos

```
Frame | USB Data
------|----------
1 | 41004101 (cmd: 0x41)
3 | 41014101 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 1: Start/Stop: START
- Frame 3: Start/Stop: START

---

# Part 3: Settings Adjustments


## device_settings_constantforce_100_to_50

```
Frame | USB Data
------|----------
263 | 4205 (cmd: 0x42)
1919 | 4205 (cmd: 0x42)
```

**Decoded Commands:**

- Frame 263: Initialize: 4205
- Frame 1919: Initialize: 4205

## device_settings_damperforces_100_to_10

```
Frame | USB Data
------|----------
279 | 4205 (cmd: 0x42)
2863 | 4205 (cmd: 0x42)
```

**Decoded Commands:**

- Frame 279: Initialize: 4205
- Frame 2863: Initialize: 4205

## device_settings_globalautocenter_from_12_to_55

```
Frame | USB Data
------|----------
267 | 4205 (cmd: 0x42)
469 | 40040000 (cmd: 0x40)
947 | 40040100 (cmd: 0x40)
1253 | 40030d00 (cmd: 0x40)
1269 | 40030e00 (cmd: 0x40)
1287 | 40030f00 (cmd: 0x40)
1305 | 40031000 (cmd: 0x40)
1313 | 40031100 (cmd: 0x40)
1327 | 40031200 (cmd: 0x40)
1345 | 40031300 (cmd: 0x40)
1359 | 40031400 (cmd: 0x40)
1391 | 40031500 (cmd: 0x40)
1413 | 40031600 (cmd: 0x40)
1433 | 40031700 (cmd: 0x40)
1473 | 40031800 (cmd: 0x40)
1531 | 40031900 (cmd: 0x40)
1553 | 40031a00 (cmd: 0x40)
1771 | 40031b00 (cmd: 0x40)
1863 | 40031c00 (cmd: 0x40)
1875 | 40031d00 (cmd: 0x40)
1897 | 40031e00 (cmd: 0x40)
1929 | 40031f00 (cmd: 0x40)
1941 | 40032000 (cmd: 0x40)
2017 | 40032100 (cmd: 0x40)
2079 | 40032200 (cmd: 0x40)
2089 | 40032300 (cmd: 0x40)
2107 | 40032400 (cmd: 0x40)
2153 | 40032500 (cmd: 0x40)
2185 | 40032600 (cmd: 0x40)
2267 | 40032700 (cmd: 0x40)
2315 | 40032800 (cmd: 0x40)
2389 | 40032900 (cmd: 0x40)
2403 | 40032a00 (cmd: 0x40)
2429 | 40032b00 (cmd: 0x40)
2445 | 40032c00 (cmd: 0x40)
2461 | 40032d00 (cmd: 0x40)
2485 | 40032e00 (cmd: 0x40)
2549 | 40032f00 (cmd: 0x40)
2569 | 40033000 (cmd: 0x40)
2589 | 40033100 (cmd: 0x40)
2615 | 40033200 (cmd: 0x40)
2623 | 40033300 (cmd: 0x40)
2635 | 40033400 (cmd: 0x40)
2653 | 40033500 (cmd: 0x40)
2661 | 40033600 (cmd: 0x40)
2749 | 40033700 (cmd: 0x40)
3523 | 4205 (cmd: 0x42)
```

**Decoded Commands:**

- Frame 267: Initialize: 4205
- Frame 469: Unknown (0x40): 40040000
- Frame 947: Unknown (0x40): 40040100
- Frame 1253: Unknown (0x40): 40030d00
- Frame 1269: Unknown (0x40): 40030e00
- Frame 1287: Unknown (0x40): 40030f00
- Frame 1305: Unknown (0x40): 40031000
- Frame 1313: Unknown (0x40): 40031100
- Frame 1327: Unknown (0x40): 40031200
- Frame 1345: Unknown (0x40): 40031300
- Frame 1359: Unknown (0x40): 40031400
- Frame 1391: Unknown (0x40): 40031500
- Frame 1413: Unknown (0x40): 40031600
- Frame 1433: Unknown (0x40): 40031700
- Frame 1473: Unknown (0x40): 40031800
- Frame 1531: Unknown (0x40): 40031900
- Frame 1553: Unknown (0x40): 40031a00
- Frame 1771: Unknown (0x40): 40031b00
- Frame 1863: Unknown (0x40): 40031c00
- Frame 1875: Unknown (0x40): 40031d00
- Frame 1897: Unknown (0x40): 40031e00
- Frame 1929: Unknown (0x40): 40031f00
- Frame 1941: Unknown (0x40): 40032000
- Frame 2017: Unknown (0x40): 40032100
- Frame 2079: Unknown (0x40): 40032200
- Frame 2089: Unknown (0x40): 40032300
- Frame 2107: Unknown (0x40): 40032400
- Frame 2153: Unknown (0x40): 40032500
- Frame 2185: Unknown (0x40): 40032600
- Frame 2267: Unknown (0x40): 40032700
- Frame 2315: Unknown (0x40): 40032800
- Frame 2389: Unknown (0x40): 40032900
- Frame 2403: Unknown (0x40): 40032a00
- Frame 2429: Unknown (0x40): 40032b00
- Frame 2445: Unknown (0x40): 40032c00
- Frame 2461: Unknown (0x40): 40032d00
- Frame 2485: Unknown (0x40): 40032e00
- Frame 2549: Unknown (0x40): 40032f00
- Frame 2569: Unknown (0x40): 40033000
- Frame 2589: Unknown (0x40): 40033100
- Frame 2615: Unknown (0x40): 40033200
- Frame 2623: Unknown (0x40): 40033300
- Frame 2635: Unknown (0x40): 40033400
- Frame 2653: Unknown (0x40): 40033500
- Frame 2661: Unknown (0x40): 40033600
- Frame 2749: Unknown (0x40): 40033700
- Frame 3523: Initialize: 4205

## device_settings_globalforce_60_to_20

```
Frame | USB Data
------|----------
305 | 4205 (cmd: 0x42)
763 | 434c (cmd: 0x43)
769 | 434a (cmd: 0x43)
777 | 4349 (cmd: 0x43)
783 | 4348 (cmd: 0x43)
787 | 4346 (cmd: 0x43)
793 | 4345 (cmd: 0x43)
803 | 4343 (cmd: 0x43)
813 | 4341 (cmd: 0x43)
817 | 4340 (cmd: 0x43)
823 | 433f (cmd: 0x43)
829 | 433d (cmd: 0x43)
837 | 433c (cmd: 0x43)
845 | 433b (cmd: 0x43)
851 | 433a (cmd: 0x43)
859 | 4338 (cmd: 0x43)
863 | 4337 (cmd: 0x43)
870 | 4336 (cmd: 0x43)
877 | 4334 (cmd: 0x43)
881 | 4333 (cmd: 0x43)
885 | 4332 (cmd: 0x43)
889 | 4331 (cmd: 0x43)
895 | 432f (cmd: 0x43)
899 | 432e (cmd: 0x43)
909 | 432d (cmd: 0x43)
917 | 432b (cmd: 0x43)
923 | 432a (cmd: 0x43)
927 | 4329 (cmd: 0x43)
933 | 4326 (cmd: 0x43)
937 | 4325 (cmd: 0x43)
943 | 4322 (cmd: 0x43)
949 | 4321 (cmd: 0x43)
965 | 4320 (cmd: 0x43)
973 | 431e (cmd: 0x43)
983 | 431d (cmd: 0x43)
995 | 431c (cmd: 0x43)
1001 | 431b (cmd: 0x43)
1013 | 4319 (cmd: 0x43)
1031 | 4318 (cmd: 0x43)
1039 | 4319 (cmd: 0x43)
2043 | 4205 (cmd: 0x42)
```

**Decoded Commands:**

- Frame 305: Initialize: 4205
- Frame 763: Unknown (0x43): 434c
- Frame 769: Unknown (0x43): 434a
- Frame 777: Unknown (0x43): 4349
- Frame 783: Unknown (0x43): 4348
- Frame 787: Unknown (0x43): 4346
- Frame 793: Unknown (0x43): 4345
- Frame 803: Unknown (0x43): 4343
- Frame 813: Unknown (0x43): 4341
- Frame 817: Unknown (0x43): 4340
- Frame 823: Unknown (0x43): 433f
- Frame 829: Unknown (0x43): 433d
- Frame 837: Unknown (0x43): 433c
- Frame 845: Unknown (0x43): 433b
- Frame 851: Unknown (0x43): 433a
- Frame 859: Unknown (0x43): 4338
- Frame 863: Unknown (0x43): 4337
- Frame 870: Unknown (0x43): 4336
- Frame 877: Unknown (0x43): 4334
- Frame 881: Unknown (0x43): 4333
- Frame 885: Unknown (0x43): 4332
- Frame 889: Unknown (0x43): 4331
- Frame 895: Unknown (0x43): 432f
- Frame 899: Unknown (0x43): 432e
- Frame 909: Unknown (0x43): 432d
- Frame 917: Unknown (0x43): 432b
- Frame 923: Unknown (0x43): 432a
- Frame 927: Unknown (0x43): 4329
- Frame 933: Unknown (0x43): 4326
- Frame 937: Unknown (0x43): 4325
- Frame 943: Unknown (0x43): 4322
- Frame 949: Unknown (0x43): 4321
- Frame 965: Unknown (0x43): 4320
- Frame 973: Unknown (0x43): 431e
- Frame 983: Unknown (0x43): 431d
- Frame 995: Unknown (0x43): 431c
- Frame 1001: Unknown (0x43): 431b
- Frame 1013: Unknown (0x43): 4319
- Frame 1031: Unknown (0x43): 4318
- Frame 1039: Unknown (0x43): 4319
- Frame 2043: Initialize: 4205

## device_settings_periodicforce_100_to_60

```
Frame | USB Data
------|----------
221 | 4205 (cmd: 0x42)
1583 | 4205 (cmd: 0x42)
```

**Decoded Commands:**

- Frame 221: Initialize: 4205
- Frame 1583: Initialize: 4205

## device_settings_springforce_100_to_30

```
Frame | USB Data
------|----------
311 | 4205 (cmd: 0x42)
1585 | 4205 (cmd: 0x42)
```

**Decoded Commands:**

- Frame 311: Initialize: 4205
- Frame 1585: Initialize: 4205

---

# Part 4: Control Panel Effects


## ctl_panel_boing

```
Frame | USB Data
------|----------
369 | 4205 (cmd: 0x42)
795 | 021c0095003fe50100 (cmd: 0x02)
796 | 41000001 (cmd: 0x41)
799 | 01002240bc02002c010e001c000000 (cmd: 0x01)
801 | 02380095003fe50100 (cmd: 0x02)
803 | 042a002000002100 (cmd: 0x04)
805 | 01012240bc02002c012a0038000000 (cmd: 0x01)
807 | 41014101 (cmd: 0x41)
1699 | 4205 (cmd: 0x42)
1701 | 41010001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 369: Initialize: 4205
- Frame 795: Upload Envelope: attack=38144ms@63, fade=485ms
- Frame 796: Start/Stop: STOP
- Frame 799: Duration/Control: type=Sine, duration=700ms
- Frame 801: Upload Envelope: attack=38144ms@63, fade=485ms
- Frame 803: Periodic/Ramp: 042a002000002100
- Frame 805: Duration/Control: type=Sine, duration=700ms
- Frame 807: Start/Stop: START
- Frame 1699: Initialize: 4205
- Frame 1701: Start/Stop: STOP

## ctl_panel_bumpy_road

```
Frame | USB Data
------|----------
187 | 4205 (cmd: 0x42)
511 | 021c0000001f00001f (cmd: 0x02)
512 | 41000001 (cmd: 0x41)
515 | 01002240dc050000000e001c000000 (cmd: 0x01)
517 | 41000001 (cmd: 0x41)
519 | 02380000001f00001f (cmd: 0x02)
521 | 042a001f00005a00 (cmd: 0x04)
523 | 01012240dc050000002a0038000000 (cmd: 0x01)
525 | 01002240e8030000000e001c000000 (cmd: 0x01)
527 | 02540000000c00000c (cmd: 0x02)
529 | 0446000c00004d01 (cmd: 0x04)
531 | 01022240e803000000460054000000 (cmd: 0x01)
533 | 41014101 (cmd: 0x41)
535 | 41024101 (cmd: 0x41)
2319 | 4205 (cmd: 0x42)
2321 | 41010001 (cmd: 0x41)
2322 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 187: Initialize: 4205
- Frame 511: Upload Envelope: attack=0ms@31, fade=2031616ms
- Frame 512: Start/Stop: STOP
- Frame 515: Duration/Control: type=Sine, duration=1500ms
- Frame 517: Start/Stop: STOP
- Frame 519: Upload Envelope: attack=0ms@31, fade=2031616ms
- Frame 521: Periodic/Ramp: 042a001f00005a00
- Frame 523: Duration/Control: type=Sine, duration=1500ms
- Frame 525: Duration/Control: type=Sine, duration=1000ms
- Frame 527: Upload Envelope: attack=0ms@12, fade=786432ms
- Frame 529: Periodic/Ramp: 0446000c00004d01
- Frame 531: Duration/Control: type=Sine, duration=1000ms
- Frame 533: Start/Stop: START
- Frame 535: Start/Stop: START
- Frame 2319: Initialize: 4205
- Frame 2321: Start/Stop: STOP
- Frame 2322: Start/Stop: STOP

## ctl_panel_car_crash

```
Frame | USB Data
------|----------
283 | 4205 (cmd: 0x42)
565 | 021c007d033fcb0236 (cmd: 0x02)
566 | 41000001 (cmd: 0x41)
569 | 010020408c0a0000000e001c000000 (cmd: 0x01)
571 | 41000001 (cmd: 0x41)
573 | 0238007d033fcb0236 (cmd: 0x02)
575 | 41000001 (cmd: 0x41)
577 | 042a0025007fd80e (cmd: 0x04)
579 | 010120408c0a0000002a0038000000 (cmd: 0x01)
581 | 01002040f00a0000000e001c000000 (cmd: 0x01)
583 | 02540000000094041b (cmd: 0x02)
585 | 0446000000009001 (cmd: 0x04)
587 | 01022040f00a000000460054000000 (cmd: 0x01)
589 | 01002140dc050000000e001c000000 (cmd: 0x01)
591 | 02700000001a00001a (cmd: 0x02)
593 | 0462001a0000dc05 (cmd: 0x04)
595 | 01032140dc05000000620070000000 (cmd: 0x01)
597 | 41014101 (cmd: 0x41)
599 | 41024101 (cmd: 0x41)
607 | 41034101 (cmd: 0x41)
3295 | 4205 (cmd: 0x42)
3297 | 41010001 (cmd: 0x41)
3298 | 41020001 (cmd: 0x41)
3301 | 41030001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 283: Initialize: 4205
- Frame 565: Upload Envelope: attack=228608ms@63, fade=3539659ms
- Frame 566: Start/Stop: STOP
- Frame 569: Duration/Control: type=Square, duration=2700ms
- Frame 571: Start/Stop: STOP
- Frame 573: Upload Envelope: attack=228608ms@63, fade=3539659ms
- Frame 575: Start/Stop: STOP
- Frame 577: Periodic/Ramp: 042a0025007fd80e
- Frame 579: Duration/Control: type=Square, duration=2700ms
- Frame 581: Duration/Control: type=Square, duration=2800ms
- Frame 583: Upload Envelope: attack=0ms@0, fade=1770644ms
- Frame 585: Periodic/Ramp: 0446000000009001
- Frame 587: Duration/Control: type=Square, duration=2800ms
- Frame 589: Duration/Control: type=Triangle, duration=1500ms
- Frame 591: Upload Envelope: attack=0ms@26, fade=1703936ms
- Frame 593: Periodic/Ramp: 0462001a0000dc05
- Frame 595: Duration/Control: type=Triangle, duration=1500ms
- Frame 597: Start/Stop: START
- Frame 599: Start/Stop: START
- Frame 607: Start/Stop: START
- Frame 3295: Initialize: 4205
- Frame 3297: Start/Stop: STOP
- Frame 3298: Start/Stop: STOP
- Frame 3301: Start/Stop: STOP

## ctl_panel_engine_start

```
Frame | USB Data
------|----------
453 | 4205 (cmd: 0x42)
1071 | 021c0055013f710500 (cmd: 0x02)
1072 | 41000001 (cmd: 0x41)
1075 | 0100204098080000000e001c000000 (cmd: 0x01)
1077 | 41000001 (cmd: 0x41)
1079 | 02380055013f710500 (cmd: 0x02)
1081 | 042a002efd001802 (cmd: 0x04)
1083 | 0101204098080000002a0038000000 (cmd: 0x01)
1085 | 01002240740e0000000e001c000000 (cmd: 0x01)
1087 | 0254000c0a00c0010c (cmd: 0x02)
1089 | 0446003200006b00 (cmd: 0x04)
1091 | 01022240740e000000460054000000 (cmd: 0x01)
1093 | 41014101 (cmd: 0x41)
1095 | 41024101 (cmd: 0x41)
4699 | 4205 (cmd: 0x42)
4701 | 41010001 (cmd: 0x41)
4702 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 453: Initialize: 4205
- Frame 1071: Upload Envelope: attack=87296ms@63, fade=1393ms
- Frame 1072: Start/Stop: STOP
- Frame 1075: Duration/Control: type=Square, duration=2200ms
- Frame 1077: Start/Stop: STOP
- Frame 1079: Upload Envelope: attack=87296ms@63, fade=1393ms
- Frame 1081: Periodic/Ramp: 042a002efd001802
- Frame 1083: Duration/Control: type=Square, duration=2200ms
- Frame 1085: Duration/Control: type=Sine, duration=3700ms
- Frame 1087: Upload Envelope: attack=658432ms@0, fade=786880ms
- Frame 1089: Periodic/Ramp: 0446003200006b00
- Frame 1091: Duration/Control: type=Sine, duration=3700ms
- Frame 1093: Start/Stop: START
- Frame 1095: Start/Stop: START
- Frame 4699: Initialize: 4205
- Frame 4701: Start/Stop: STOP
- Frame 4702: Start/Stop: STOP

## ctl_panel_explosion

```
Frame | USB Data
------|----------
221 | 4205 (cmd: 0x42)
651 | 021c0045012a160200 (cmd: 0x02)
652 | 41000001 (cmd: 0x41)
655 | 010022405a0a0000000e001c000000 (cmd: 0x01)
656 | 41000001 (cmd: 0x41)
659 | 02380045012a160200 (cmd: 0x02)
661 | 41000001 (cmd: 0x41)
663 | 042a001300006400 (cmd: 0x04)
665 | 010122405a0a0000002a0038000000 (cmd: 0x01)
667 | 01002240540b0000000e001c000000 (cmd: 0x01)
669 | 025400000022000022 (cmd: 0x02)
671 | 04460022f67e7002 (cmd: 0x04)
673 | 01022240540b000000460054000000 (cmd: 0x01)
675 | 0100204052030000000e001c000000 (cmd: 0x01)
677 | 02700000003f00003f (cmd: 0x02)
679 | 0462003f00000203 (cmd: 0x04)
681 | 010320405203000000620070000000 (cmd: 0x01)
683 | 41014101 (cmd: 0x41)
685 | 41024101 (cmd: 0x41)
691 | 41034101 (cmd: 0x41)
3851 | 4205 (cmd: 0x42)
3853 | 41010001 (cmd: 0x41)
3854 | 41020001 (cmd: 0x41)
3857 | 41030001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 221: Initialize: 4205
- Frame 651: Upload Envelope: attack=83200ms@42, fade=534ms
- Frame 652: Start/Stop: STOP
- Frame 655: Duration/Control: type=Sine, duration=2650ms
- Frame 656: Start/Stop: STOP
- Frame 659: Upload Envelope: attack=83200ms@42, fade=534ms
- Frame 661: Start/Stop: STOP
- Frame 663: Periodic/Ramp: 042a001300006400
- Frame 665: Duration/Control: type=Sine, duration=2650ms
- Frame 667: Duration/Control: type=Sine, duration=2900ms
- Frame 669: Upload Envelope: attack=0ms@34, fade=2228224ms
- Frame 671: Periodic/Ramp: 04460022f67e7002
- Frame 673: Duration/Control: type=Sine, duration=2900ms
- Frame 675: Duration/Control: type=Square, duration=850ms
- Frame 677: Upload Envelope: attack=0ms@63, fade=4128768ms
- Frame 679: Periodic/Ramp: 0462003f00000203
- Frame 681: Duration/Control: type=Square, duration=850ms
- Frame 683: Start/Stop: START
- Frame 685: Start/Stop: START
- Frame 691: Start/Stop: START
- Frame 3851: Initialize: 4205
- Frame 3853: Start/Stop: STOP
- Frame 3854: Start/Stop: STOP
- Frame 3857: Start/Stop: STOP

## ctl_panel_flat_tire

```
Frame | USB Data
------|----------
413 | 4205 (cmd: 0x42)
853 | 021c00d90000aa022c (cmd: 0x02)
854 | 41000001 (cmd: 0x41)
857 | 01002040e2040000000e001c000000 (cmd: 0x01)
859 | 41000001 (cmd: 0x41)
861 | 023800d90000aa022c (cmd: 0x02)
863 | 042a000500002202 (cmd: 0x04)
865 | 01012040e2040000002a0038000000 (cmd: 0x01)
867 | 0100004084030000000e001c000000 (cmd: 0x01)
869 | 025400000026000026 (cmd: 0x02)
871 | 034600ed (cmd: 0x03)
873 | 010200408403000000460054000000 (cmd: 0x01)
875 | 41014101 (cmd: 0x41)
877 | 41024101 (cmd: 0x41)
2271 | 4205 (cmd: 0x42)
2273 | 41010001 (cmd: 0x41)
2274 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 413: Initialize: 4205
- Frame 853: Upload Envelope: attack=55552ms@0, fade=2884266ms
- Frame 854: Start/Stop: STOP
- Frame 857: Duration/Control: type=Square, duration=1250ms
- Frame 859: Start/Stop: STOP
- Frame 861: Upload Envelope: attack=55552ms@0, fade=2884266ms
- Frame 863: Periodic/Ramp: 042a000500002202
- Frame 865: Duration/Control: type=Square, duration=1250ms
- Frame 867: Duration/Control: type=Constant, duration=900ms
- Frame 869: Upload Envelope: attack=0ms@38, fade=2490368ms
- Frame 871: Constant Force: level=237 (LEFT, mag=19)
- Frame 873: Duration/Control: type=Constant, duration=900ms
- Frame 875: Start/Stop: START
- Frame 877: Start/Stop: START
- Frame 2271: Initialize: 4205
- Frame 2273: Start/Stop: STOP
- Frame 2274: Start/Stop: STOP

## ctl_panel_gong

```
Frame | USB Data
------|----------
345 | 4205 (cmd: 0x42)
725 | 021c00ee022ca60e04 (cmd: 0x02)
726 | 41000001 (cmd: 0x41)
729 | 0100224094110000000e001c000000 (cmd: 0x01)
730 | 41000001 (cmd: 0x41)
733 | 023800ee022ca60e04 (cmd: 0x02)
735 | 042a001600000a00 (cmd: 0x04)
737 | 0101224094110000002a0038000000 (cmd: 0x01)
739 | 010022407d000000000e001c000000 (cmd: 0x01)
741 | 025400000021000021 (cmd: 0x02)
743 | 04460021001ffa00 (cmd: 0x04)
745 | 010222407d00000000460054000000 (cmd: 0x01)
747 | 41014101 (cmd: 0x41)
749 | 41024101 (cmd: 0x41)
2149 | 4205 (cmd: 0x42)
2151 | 41010001 (cmd: 0x41)
2152 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 345: Initialize: 4205
- Frame 725: Upload Envelope: attack=192000ms@44, fade=265894ms
- Frame 726: Start/Stop: STOP
- Frame 729: Duration/Control: type=Sine, duration=4500ms
- Frame 730: Start/Stop: STOP
- Frame 733: Upload Envelope: attack=192000ms@44, fade=265894ms
- Frame 735: Periodic/Ramp: 042a001600000a00
- Frame 737: Duration/Control: type=Sine, duration=4500ms
- Frame 739: Duration/Control: type=Sine, duration=125ms
- Frame 741: Upload Envelope: attack=0ms@33, fade=2162688ms
- Frame 743: Periodic/Ramp: 04460021001ffa00
- Frame 745: Duration/Control: type=Sine, duration=125ms
- Frame 747: Start/Stop: START
- Frame 749: Start/Stop: START
- Frame 2149: Initialize: 4205
- Frame 2151: Start/Stop: STOP
- Frame 2152: Start/Stop: STOP

## ctl_panel_magnetic_field

```
Frame | USB Data
------|----------
223 | 4205 (cmd: 0x42)
571 | 021c0030010e120200 (cmd: 0x02)
572 | 41000001 (cmd: 0x41)
575 | 01002040e8030000000e001c000000 (cmd: 0x01)
577 | 41000001 (cmd: 0x41)
579 | 02380030010e120200 (cmd: 0x02)
581 | 042a003f00001f00 (cmd: 0x04)
583 | 01012040e8030000002a0038000000 (cmd: 0x01)
585 | 01002040f4010000000e001c000000 (cmd: 0x01)
587 | 02540000003fef0000 (cmd: 0x02)
589 | 0446003f00f10804 (cmd: 0x04)
591 | 01022040f401000000460054000000 (cmd: 0x01)
593 | 41014101 (cmd: 0x41)
595 | 41024101 (cmd: 0x41)
2225 | 4205 (cmd: 0x42)
2227 | 41010001 (cmd: 0x41)
2228 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 223: Initialize: 4205
- Frame 571: Upload Envelope: attack=77824ms@14, fade=530ms
- Frame 572: Start/Stop: STOP
- Frame 575: Duration/Control: type=Square, duration=1000ms
- Frame 577: Start/Stop: STOP
- Frame 579: Upload Envelope: attack=77824ms@14, fade=530ms
- Frame 581: Periodic/Ramp: 042a003f00001f00
- Frame 583: Duration/Control: type=Square, duration=1000ms
- Frame 585: Duration/Control: type=Square, duration=500ms
- Frame 587: Upload Envelope: attack=0ms@63, fade=239ms
- Frame 589: Periodic/Ramp: 0446003f00f10804
- Frame 591: Duration/Control: type=Square, duration=500ms
- Frame 593: Start/Stop: START
- Frame 595: Start/Stop: START
- Frame 2225: Initialize: 4205
- Frame 2227: Start/Stop: STOP
- Frame 2228: Start/Stop: STOP

## ctl_panel_ocean_waves

```
Frame | USB Data
------|----------
151 | 4205 (cmd: 0x42)
483 | 021c00000035000035 (cmd: 0x02)
484 | 41000001 (cmd: 0x41)
487 | 01002240c6110000000e001c000000 (cmd: 0x01)
489 | 023800000035000035 (cmd: 0x02)
491 | 042a00350000d007 (cmd: 0x04)
493 | 01012240c6110000002a0038000000 (cmd: 0x01)
495 | 41014101 (cmd: 0x41)
5925 | 4205 (cmd: 0x42)
5927 | 41010001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 151: Initialize: 4205
- Frame 483: Upload Envelope: attack=0ms@53, fade=3473408ms
- Frame 484: Start/Stop: STOP
- Frame 487: Duration/Control: type=Sine, duration=4550ms
- Frame 489: Upload Envelope: attack=0ms@53, fade=3473408ms
- Frame 491: Periodic/Ramp: 042a00350000d007
- Frame 493: Duration/Control: type=Sine, duration=4550ms
- Frame 495: Start/Stop: START
- Frame 5925: Initialize: 4205
- Frame 5927: Start/Stop: STOP

## ctl_panel_punch_hit

```
Frame | USB Data
------|----------
689 | 4205 (cmd: 0x42)
1113 | 021c0000003f00003f (cmd: 0x02)
1114 | 41000001 (cmd: 0x41)
1117 | 01002240fa000000000e001c000000 (cmd: 0x01)
1118 | 41000001 (cmd: 0x41)
1121 | 02380000003f00003f (cmd: 0x02)
1123 | 042a003f003fee02 (cmd: 0x04)
1125 | 01012240fa000000002a0038000000 (cmd: 0x01)
1127 | 01004140ee020000000e001c000000 (cmd: 0x01)
1129 | 0546005a5a000000006464 (cmd: 0x05)
1131 | 0554000000000000006464 (cmd: 0x05)
1133 | 01024140ee02000000460054000000 (cmd: 0x01)
1135 | 41014101 (cmd: 0x41)
1137 | 41024101 (cmd: 0x41)
2035 | 4205 (cmd: 0x42)
2037 | 41010001 (cmd: 0x41)
2038 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 689: Initialize: 4205
- Frame 1113: Upload Envelope: attack=0ms@63, fade=4128768ms
- Frame 1114: Start/Stop: STOP
- Frame 1117: Duration/Control: type=Sine, duration=250ms
- Frame 1118: Start/Stop: STOP
- Frame 1121: Upload Envelope: attack=0ms@63, fade=4128768ms
- Frame 1123: Periodic/Ramp: 042a003f003fee02
- Frame 1125: Duration/Control: type=Sine, duration=250ms
- Frame 1127: Duration/Control: type=Damper, duration=750ms
- Frame 1129: Condition: 0546005a5a000000006464
- Frame 1131: Condition: 0554000000000000006464
- Frame 1133: Duration/Control: type=Damper, duration=750ms
- Frame 1135: Start/Stop: START
- Frame 1137: Start/Stop: START
- Frame 2035: Initialize: 4205
- Frame 2037: Start/Stop: STOP
- Frame 2038: Start/Stop: STOP

## ctl_panel_turbo

```
Frame | USB Data
------|----------
257 | 4205 (cmd: 0x42)
711 | 41000001 (cmd: 0x41)
712 | 01002240e20400e8030e001c000000 (cmd: 0x01)
715 | 023800900100520300 (cmd: 0x02)
717 | 41000001 (cmd: 0x41)
719 | 042a001600001400 (cmd: 0x04)
721 | 01012240e20400e8032a0038000000 (cmd: 0x01)
723 | 01002040fa00000e060e001c000000 (cmd: 0x01)
725 | 025400b10000480008 (cmd: 0x02)
727 | 0446002c0000c201 (cmd: 0x04)
729 | 01022040fa00000e06460054000000 (cmd: 0x01)
731 | 41014101 (cmd: 0x41)
733 | 41024101 (cmd: 0x41)
1871 | 4205 (cmd: 0x42)
1873 | 41010001 (cmd: 0x41)
1874 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 257: Initialize: 4205
- Frame 711: Start/Stop: STOP
- Frame 712: Duration/Control: type=Sine, duration=1250ms
- Frame 715: Upload Envelope: attack=102400ms@0, fade=850ms
- Frame 717: Start/Stop: STOP
- Frame 719: Periodic/Ramp: 042a001600001400
- Frame 721: Duration/Control: type=Sine, duration=1250ms
- Frame 723: Duration/Control: type=Square, duration=250ms
- Frame 725: Upload Envelope: attack=45312ms@0, fade=524360ms
- Frame 727: Periodic/Ramp: 0446002c0000c201
- Frame 729: Duration/Control: type=Square, duration=250ms
- Frame 731: Start/Stop: START
- Frame 733: Start/Stop: START
- Frame 1871: Initialize: 4205
- Frame 1873: Start/Stop: STOP
- Frame 1874: Start/Stop: STOP

## ctl_panel_wisplash

```
Frame | USB Data
------|----------
155 | 4205 (cmd: 0x42)
373 | 021c00000028000028 (cmd: 0x02)
374 | 41000001 (cmd: 0x41)
377 | 01002240e8030000000e001c000000 (cmd: 0x01)
379 | 41000001 (cmd: 0x41)
381 | 023800000028000028 (cmd: 0x02)
383 | 042a002800002202 (cmd: 0x04)
385 | 01012240e8030000002a0038000000 (cmd: 0x01)
387 | 01002240dc050000000e001c000000 (cmd: 0x01)
389 | 025400dc0501e8033f (cmd: 0x02)
391 | 044600370000cf03 (cmd: 0x04)
393 | 01022240dc05000000460054000000 (cmd: 0x01)
395 | 41014101 (cmd: 0x41)
397 | 41024101 (cmd: 0x41)
2353 | 4205 (cmd: 0x42)
2355 | 41010001 (cmd: 0x41)
2356 | 41020001 (cmd: 0x41)
```

**Decoded Commands:**

- Frame 155: Initialize: 4205
- Frame 373: Upload Envelope: attack=0ms@40, fade=2621440ms
- Frame 374: Start/Stop: STOP
- Frame 377: Duration/Control: type=Sine, duration=1000ms
- Frame 379: Start/Stop: STOP
- Frame 381: Upload Envelope: attack=0ms@40, fade=2621440ms
- Frame 383: Periodic/Ramp: 042a002800002202
- Frame 385: Duration/Control: type=Sine, duration=1000ms
- Frame 387: Duration/Control: type=Sine, duration=1500ms
- Frame 389: Upload Envelope: attack=384000ms@1, fade=4129768ms
- Frame 391: Periodic/Ramp: 044600370000cf03
- Frame 393: Duration/Control: type=Sine, duration=1500ms
- Frame 395: Start/Stop: START
- Frame 397: Start/Stop: START
- Frame 2353: Initialize: 4205
- Frame 2355: Start/Stop: STOP
- Frame 2356: Start/Stop: STOP

---

# Analysis Complete

Total captures analyzed: 21
