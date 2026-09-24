import io

p = r"C:\Users\TY\Documents\Doubao\YEELIGHT\yeelight_rf_bridge_v2.ino"
with io.open(p, "r", encoding="utf-8") as f:
    c = f.read()

old = """  h += "<div class=card><a href='/wifi'>WiFi Settings</a> | <a href='/debug'>RF Debug</a> | <a href='/update'>Firmware Update</a></div>";"""

new_lines = [
'  h += "<div class=card>";',
'  h += "<div style=\'font-size:13px;color:#888\'>Firmware v"; h += FW_VERSION; h += "</div>";',
'  h += "<div style=\'margin-top:8px\'><a href=\'/wifi\'>WiFi</a> | <a href=\'/debug\'>RF Debug</a> | <a href=\'/update\'>Upload .bin</a></div>";',
'  h += "<div style=\'margin-top:10px\'><button onclick=\'checkUpdate()\' style=\'width:100%\'>Check for Update</button>";',
'  h += "<div id=upd style=\'margin-top:8px;font-size:14px\'></div></div>";',
'  h += "</div>";',
'  h += "<script>";',
'  h += "async function checkUpdate(){";',
'  h += "var d=document.getElementById(\'upd\');d.textContent=\'Checking...\';";',
'  h += "try{var r=await fetch(\'/checkupdate\');var j=await r.json();";',
'  h += "if(j.error){d.innerHTML=\'Error: \'+j.error;return;}";',
'  h += "if(j.hasUpdate){var a=document.createElement(\'a\');a.href=\'/doupdate?url=\'+encodeURIComponent(j.url);";',
'  h += "a.innerHTML=\'<button style=background:#30d158;width:100%;margin-top:6px>Install v\'+j.latest+\'</button>\';";',
'  h += "d.innerHTML=\'New version available! \';d.appendChild(a);}";',
'  h += "else{d.innerHTML=\'Up to date (v\'+j.current+\')\';}";',
'  h += "}catch(e){d.textContent=\'Network error\';}";',
'  h += "}";',
'  h += "</script>";',
]

new = "\n".join(new_lines)
c = c.replace(old, new)

with io.open(p, "w", encoding="utf-8", newline="\n") as f:
    f.write(c)
print("done")
