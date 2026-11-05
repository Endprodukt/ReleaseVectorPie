# deploy.ps1 — copy the sources to the VectorPie cabinet, build and package
# Release there (tools/build-on-cabinet.sh) and install it over the old one,
# keeping its save/ folder. Run from anywhere on the Windows PC.
#   powershell -File tools\deploy.ps1 [-Cabinet 192.168.0.122]
param([string]$Cabinet = "192.168.0.122")
$ErrorActionPreference = "Stop"
$key = "$env:USERPROFILE\.ssh\id_ed25519_vectorpie"
$root = "F:\Release Game"
$tar = "$env:TEMP\release-src.tar.gz"
$version = (Select-String -Path "$root\release-vp\package\game.ini" -Pattern '^version\s*=\s*(\S+)').Matches[0].Groups[1].Value

tar -czf $tar --exclude=build --exclude=build-pc --exclude=build-pi --exclude=build-linux --exclude=.git `
    -C "$root\VectorPie" sdk -C $root release-vp
scp -i $key -o BatchMode=yes $tar "pi@${Cabinet}:release-build/src.tar.gz"
ssh -i $key -o BatchMode=yes "pi@$Cabinet" @"
set -e
cd ~/release-build
rm -rf sdk release-vp && tar -xzf src.tar.gz
cp release-vp/tools/build-on-cabinet.sh . && sed -i 's/\r`$//' build-on-cabinet.sh
sh build-on-cabinet.sh 2>&1 | tail -3
cd /persistent/games
[ -d release/save ] && mv release/save /tmp/release-save
rm -rf release
tar -xzf ~/release-build/build-game/release-$version.vpk -C /persistent/games/
[ -d /tmp/release-save ] && mv /tmp/release-save release/save || mkdir -p release/save
grep version release/game.ini
"@
