cat chinese_ui.txt lang/zh_CN.ini lang/zh_TW.ini lang/ja_JP.ini > chinese.txt && cat korean-ui.txt lang/ko_KR.ini > korean.txt && ./native/tools/build/atlastool atlasscript.txt ui 8888 && cp ui_atlas.zim assets && mv ui_atlas.zim android/assets && mv ui_atlas.cpp ui_atlas.h UI
./native/tools/build/atlastool atlasscript_lowmem.txt ui 8888 && mv ui_atlas.zim assets/ui_atlas_lowmem.zim && mv ui_atlas.cpp UI/ui_atlas_lowmem.cpp && rm ui_atlas.h
