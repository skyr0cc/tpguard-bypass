
#!/usr/bin/env bash

PLACE_ID="109983668079237"
URI="roblox://experiences/start?placeId=${PLACE_ID}"

P='\033[38;5;135m'
LP='\033[38;5;177m'
W='\033[1;37m'
G='\033[38;5;245m'
D='\033[38;5;239m'
R='\033[0m'

while true; do
    clear

    printf "\n"
    printf "  ${P}  ███████╗██╗  ██╗██╗   ██╗██████╗  ██████╗${R}\n"
    printf "  ${P}  ██╔════╝██║ ██╔╝╚██╗ ██╔╝██╔══██╗██╔═══██╗${R}\n"
    printf "  ${P}  ███████╗█████╔╝  ╚████╔╝ ██████╔╝██║   ██║${R}\n"
    printf "  ${P}  ╚════██║██╔═██╗   ╚██╔╝  ██╔══██╗██║   ██║${R}\n"
    printf "  ${P}  ███████║██║  ██╗   ██║   ██║  ██║╚██████╔╝${R}\n"
    printf "  ${P}  ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝${R}\n"
    printf "\n"
    printf "  ${W}skyro${R}${P}.wtf${R}\n"
    printf "  ${D}────────────────────────────────────────────${R}\n\n"

    printf "  ${G}APPLICATION${R}\n"
    printf "  ${W}Roblox Server Launcher${R}\n"
    printf "  ${G}PLACE ID${R}   ${W}${PLACE_ID}${R}\n\n"

    printf "  ${D}┌──────────────────────────────────────────┐${R}\n"
    printf "  ${D}│${R}  ${P}01${R}  ${W}Launch experience${R}                  ${D}│${R}\n"
    printf "  ${D}│${R}  ${P}02${R}  ${G}Exit${R}                                ${D}│${R}\n"
    printf "  ${D}└──────────────────────────────────────────┘${R}\n\n"

    read -rp "  ${P}skyro${R} ${D}›${R} " choice

    case "$choice" in
        1|01)
            xdg-open "$URI" >/dev/null 2>&1 &
            ;;
        2|02)
            clear
            exit 0
            ;;
        *)
            ;;
    esac
done
