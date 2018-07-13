# usb-trigger
Simple tool for executing program on USB device detection

Examples:

    usb-trigger --vid 0781 --pid 0x5580 --exec "echo device detected"
    
    usb-trigger --vid 0x12d1 --pid 0x1f01 --exec "usb_modeswitch -J -v 0x12d1 -p 0x1f01"

