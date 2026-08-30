#!/usr/bin/env python3
"""Add backup.import_done to all locales"""

import os

LOCALES_DIR = "src/locales"

TRANSLATIONS = {
    'de': "Konfiguration erfolgreich importiert",
    'es': "Configuración importada correctamente",
    'fr': "Configuration importée avec succès",
    'pt': "Configuração importada com sucesso",
    'ru': "Конфигурация успешно импортирована",
    'ja': "設定が正常にインポートされました",
    'ko': "구성을 성공적으로 가져왔습니다",
    'zh': "配置已成功导入",
    'ar': "تم استيراد التكوين بنجاح",
    'hi': "कॉन्फ़िगरेशन सफलतापूर्वक आयात किया गया",
    'en': "Configuration imported successfully"
}

for filename in os.listdir(LOCALES_DIR):
    if filename.endswith('.js'):
        filepath = os.path.join(LOCALES_DIR, filename)
        lang = filename.replace('.js', '')
        
        with open(filepath, 'r') as f:
            content = f.read()
        
        # Check if key already exists
        if "'backup.import_done'" in content:
            print(f"  {filename}: key already exists")
            continue
        
        # Get the translation
        trans = TRANSLATIONS.get(lang, TRANSLATIONS['en'])
        
        # Insert before backup.error.html
        if "'backup.error.html'" in content:
            content = content.replace(
                "'backup.error.html'",
                f"'backup.import_done': '{trans}',\n    'backup.error.html'"
            )
            with open(filepath, 'w') as f:
                f.write(content)
            print(f"  {filename}: Added backup.import_done")
        else:
            # Fallback: insert before closing brace
            content = content.replace(
                "  }\n};",
                f"    'backup.import_done': '{trans}'\n  }}\n}};"
            )
            with open(filepath, 'w') as f:
                f.write(content)
            print(f"  {filename}: Added backup.import_done (fallback)")

print("\n✅ Done!")
