#!/usr/bin/env bash
# Exécuter depuis une archive extraite : sudo bash deploy/install.sh
set -euo pipefail
if [[ $EUID -ne 0 ]]; then
  printf '%s\n' 'Lancer ce script avec sudo.' >&2
  exit 1
fi
for command in node systemctl curl flock; do
  command -v "$command" >/dev/null || { printf 'Commande manquante : %s\n' "$command" >&2; exit 1; }
done
exec 9>/run/lock/infhome-install.lock
flock -n 9 || { printf '%s\n' 'Une installation est déjà en cours.' >&2; exit 1; }
node_path=$(command -v node)
if [[ "$node_path" != /usr/bin/node && "$node_path" != /usr/local/bin/node ]]; then
  printf '%s\n' 'Installer Node globalement dans /usr/bin/node ou /usr/local/bin/node (pas via nvm).' >&2
  exit 1
fi
"$node_path" -e 'if (![22,24,26].includes(Number(process.versions.node.split(".")[0]))) { console.error("Node maintenu requis : installer Node 22 LTS ou une version LTS ultérieure compatible avec le Pi."); process.exit(1); }'
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ -f "$source_dir/dist/index.js" ]] || { printf '%s\n' 'Archive incomplète.' >&2; exit 1; }
"$node_path" --check "$source_dir/dist/index.js"
id infhome-api &>/dev/null || useradd --system --user-group --home-dir /var/lib/infhome --shell /usr/sbin/nologin infhome-api
install -d -m 0755 /opt/infhome/releases
install -d -m 0750 -o root -g infhome-api /etc/infhome
install -d -m 0750 -o infhome-api -g infhome-api /var/lib/infhome
if [[ ! -f /etc/infhome/infhome.env ]]; then
  install -m 0640 -o root -g infhome-api "$source_dir/.env.example" /etc/infhome/infhome.env
fi
"$node_path" --env-file=/etc/infhome/infhome.env -e 'require(process.argv[1]).configuration()' "$source_dir/dist/config.js"
address=$("$node_path" --env-file=/etc/infhome/infhome.env -e 'let host=process.env.HOST || "0.0.0.0"; if(host==="0.0.0.0") host="127.0.0.1"; if(host==="::") host="[::1]"; process.stdout.write(`http://${host}:${process.env.PORT || "8080"}`)')
release=$(mktemp -d /opt/infhome/releases/release-XXXXXXXX)
chmod 0755 "$release"
cp -R "$source_dir/dist" "$release/dist"
chmod -R a+rX "$release/dist"
previous=$(readlink -f /opt/infhome/current || true)
unit=/etc/systemd/system/infhome-api.service
backup=$(mktemp)
had_unit=false
if [[ -f "$unit" ]]; then cp "$unit" "$backup"; had_unit=true; fi
trap 'rm -f "$backup"' EXIT
install -m 0644 "$source_dir/deploy/infhome-api.service" "$unit"
if [[ "$node_path" == /usr/local/bin/node ]]; then
  sed -i 's|ExecStart=/usr/bin/node |ExecStart=/usr/local/bin/node |' "$unit"
fi
ln -s "$release" /opt/infhome/current.next
mv -Tf /opt/infhome/current.next /opt/infhome/current
systemctl daemon-reload
healthy=false
if systemctl restart infhome-api; then
  for attempt in {1..15}; do
    if curl --noproxy '*' --fail --silent --max-time 2 "$address/api/v1/dashboard" >/dev/null && systemctl is-active --quiet infhome-api; then
      healthy=true
      break
    fi
    sleep 1
  done
fi
if [[ "$healthy" != true ]]; then
  printf '%s\n' 'Échec du contrôle HTTP. Restauration de la version précédente.' >&2
  systemctl stop infhome-api || true
  if [[ "$had_unit" == true ]]; then cp "$backup" "$unit"; else rm -f "$unit"; fi
  if [[ -n "$previous" && -d "$previous" ]]; then
    ln -s "$previous" /opt/infhome/current.rollback
    mv -Tf /opt/infhome/current.rollback /opt/infhome/current
    systemctl daemon-reload
    systemctl restart infhome-api || true
  else
    rm -f /opt/infhome/current
    systemctl daemon-reload
  fi
  exit 1
fi
systemctl enable infhome-api
printf 'API installée : %s\nLogs : journalctl -u infhome-api -f\n' "$release"
