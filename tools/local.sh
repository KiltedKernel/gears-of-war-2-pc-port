# Sourced by the bash scripts: GOW2_GAME / GOW2_XENIA from tools/local.cmd (written by tools/setup.ps1) when they are
# not set already. Without local.cmd the scripts keep their own defaults.
_gow2_local="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/local.cmd"
if [ -f "$_gow2_local" ]; then
  while IFS= read -r _l; do
    _l=${_l%$'\r'}
    case "$_l" in
      'set "GOW2_GAME='*)  [ -n "${GOW2_GAME:-}" ]  || { _v=${_l#set \"GOW2_GAME=};  GOW2_GAME=$(cygpath -u "${_v%\"}" 2>/dev/null || echo "${_v%\"}"); export GOW2_GAME; } ;;
      'set "GOW2_XENIA='*) [ -n "${GOW2_XENIA:-}" ] || { _v=${_l#set \"GOW2_XENIA=}; GOW2_XENIA=$(cygpath -u "${_v%\"}" 2>/dev/null || echo "${_v%\"}"); export GOW2_XENIA; } ;;
    esac
  done < "$_gow2_local"
fi
unset _gow2_local _l _v
