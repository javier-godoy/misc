A script for polling AXFR.

```
#!/bin/bash

DOMAIN=$1
SERVER=$2

if [[ -z "$1" ]] || [[ -z "$2" ]]; then
  echo Use $0 domain dns-server
  exit 1
fi

dig_json() {
  local type=$1

  RAW=$(dig -t$type $DOMAIN @$SERVER +noall +answer)

  echo "$RAW" | grep -v '^;' |grep -v '^$' | tr '\t' \  | tr -s \  \
    | awk -v OFS='\t' '{printf "%s\t%s\t%s\t%s\t%s", $1,$2,$3,$4,$5}{for (i=6; i<=NF; i++) {printf " %s", $i}; print ""}'\
    | jq --raw-input 'split("\t")|{name:.[0],ttl:.[1],class:.[2],type:.[3],target:.[4]}
                     | .target=(.target|gsub("\" \"";"")) | .target=(.target|gsub("\"";""))'
}

parse_soa() {
 cat | jq --slurp ' first(.[]| select((.class=="IN") and (.type=="SOA")))
                  | .ttl as $ttl
                  | .target | split("\\s";null)
                  | {mname:.[0], serial:.[2], refresh:.[3], retry:.[4], expire:.[5], ttl:$ttl}'
}

get_soa() {
  SOA1=$(dig_json soa | parse_soa)
  while [[ -z "$SOA1" ]]; do
    SOA1=$(dig_json soa | parse_soa)
    sleep 10
  done

  retry=$(echo $SOA1  | jq --raw-output '.retry')
  refresh=$(echo $SOA1| jq --raw-output '.refresh')
  mname=$(echo $SOA1  | jq --raw-output '.mname')
  serial=$(echo $SOA1 | jq --raw-output '.serial')
  ttl=$(echo $SOA1 | jq --raw-output '.ttl')
}

log_soa() {
  echo "[$(date -Iseconds|cut -d+ -f1)]" SOA $serial@$mname >&2
}

get_axfr() {
  AXFR=$(dig_json axfr)
  while [[ -z "$AXFR" ]]; do
    echo AXFR failed, retry
    sleep $retry
    AXFR=$(dig_json axfr)
  done

  SOA2=$(echo $AXFR | parse_soa)
  mname=$(echo $SOA2  | jq --raw-output '.mname')
  serial=$(echo $SOA2 | jq --raw-output '.serial')
  expire=$(echo $SOA  | jq --raw-output '.expire')
  echo "[$(date -Iseconds|cut -d+ -f1)]" AXFR $serial@$mname >&2
}

apply_axfr() {
  echo $AXFR > axfr
  echo $SERIAL $MNAME > serial
  echo $AXFR
  echo -ne '\0'
}

load_axfr() {
  if [[ -f serial ]] && [[ -f axfr ]]; then
    IFS=' ' read -r SERIAL MNAME < ./serial
    AXFR=$(<./axfr)
    echo "[$(date -Iseconds|cut -d+ -f1)]" LOAD $SERIAL@$MNAME >&2
    apply_axfr
  else
    SERIAL=0000000000
    MNAME=invalid
  fi
}


load_axfr

while true; do
  get_soa
  log_soa
  if [[ ${SERIAL} -ge ${serial} ]] && [[ "$MNAME" == "$mname" ]]; then
    while [[ ${SERIAL} -ge ${serial} ]] && [[ "$MNAME" == "$mname" ]]; do
      sleep $ttl
      get_soa
    done
    log_soa
  fi

  get_axfr
  if [[ ${SERIAL} -lt ${serial} ]] || [[ "$MNAME" != "$mname" ]]; then
    SERIAL=$serial
    MNAME=$mname
    apply_axfr
    sleep $refresh
  else
    sleep $retry
  fi
done
```
