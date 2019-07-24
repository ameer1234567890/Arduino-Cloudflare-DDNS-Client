#### Arduino-Cloudflare-DDNS-Client
A Cloudflare DDNS client on an Arduino (ESP8266)

This does DDNS (Dynamic DNS) updates on the Cloudflare API endpoint, on an Arduino (ESP8266)

* Step 1: Fill in `EMAIL`, `TOKEN` and `SUBDOMAIN`. Your API token is here: `https://dash.cloudflare.com/profile/api-tokens`. Make sure the token is the Global token, or has these permissions: #zone:read, #dns_record:read, #dns_records:edit
* Step 2: Create an A record on Cloudflare with the subdomain you chose
* Step 3: Run `curl -s -H 'Content-Type:application/json' -H 'X-Auth-Key:TOKEN' -H 'X-Auth-Email:EMAIL' https://api.cloudflare.com/client/v4/zones?name=DOMAIN | sed -e 's/[{}]/\n/g' | grep '"name":"'"DOMAIN"'"' | sed -e 's/,/\n/g' | grep '"id":"' | cut -d'"' -f4 | awk '{print "ZONE_ID: "$1}'`. to obtain `ZONE_ID`. Remember to replace `TOKEN`, `EMAIL` & `DOMAIN` with your details.
* Step 4: Run `curl -s -H 'Content-Type:application/json' -H 'X-Auth-Key:TOKEN' -H 'X-Auth-Email:EMAIL' https://api.cloudflare.com/client/v4/zones/ZONE_ID/dns_records | sed -e 's/[{}]/\n/g' | grep '"name":"'"$SUBDOMAIN"'.'"$DOMAIN"'"' | sed -e 's/,/\n/g' | grep '"id":"' | cut -d'"' -f4 | awk '{print "REC_ID: "$1}'`. to obtain `REC_ID`. Remember to replace `TOKEN`, `EMAIL`, `ZONE_ID`, `SUBDOMAIN` & `DOMAIN` with your details.
* Step 4: Enter the `ZONE_ID` & `REC_ID` obtained from above steps, in Secrets.h file.
* Step 5: Compile and flash to your choice of ESP8266 board.
