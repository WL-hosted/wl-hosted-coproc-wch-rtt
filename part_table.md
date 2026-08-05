| name | flash_name | offset | len | usage |
| --- | --- | --- | --- | --- |
| bl_s1 | onchip | 0 | 2k | jump to bl s2 & put flash table |
| app_s1 | onchip | 2k | 222k | app slot 1 |
| app_s2 | onchip | 224k | 222k | app slot 2 |
| easyflash | onchip | 446k | 2k | easyflash |
| bl_s2 | onchip | 448k | 32k | actual bootloader, copy s2 fw to s1 |

