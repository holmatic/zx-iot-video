set(COMPONENT_SRCS "main.c" "file_server.c" "video_attr.c" "signal_to_zx.c" "lcd_display.c" "vga_display.c" "led_matrix.c" "iis_videosig.c" "signal_from_zx.c" "zx_server.c" "zx_serv_dialog.c" "wifi_sta.c" "zx_file_img.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

set(COMPONENT_EMBED_FILES "favicon.ico" "upload_script.html")

register_component()
