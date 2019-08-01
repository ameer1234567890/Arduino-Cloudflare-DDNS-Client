# Arduino-Cloudflare-DDNS-Client
A Cloudflare DDNS client on an Arduino (ESP8266)

This does DDNS (Dynamic DNS) updates on the Cloudflare API endpoint, on an Arduino (ESP8266)

* Step 1: Fill in `EMAIL`, `TOKEN`, `DOMAIN` and `SUBDOMAIN`. Your API token is here: `https://dash.cloudflare.com/profile/api-tokens`. Make sure the token is the Global token, or has these permissions: #zone:read, #dns_record:read, #dns_records:edit
* Step 2: Create an A record on Cloudflare with the subdomain you choose.
* Step 5: Compile and flash to your choice of ESP8266 board.
