$loc = $(pwd)
cd release
rm coinyecoin.conf
touch coinyecoin.conf
chmod 775 coinyecoin.conf
echo "server=1" >> coinyecoin.conf
echo "daemon=1" >> coinyecoin.conf
echo "listen=1" >> coinyecoin.conf
echo "rpcport=8333" >> coinyecoin.conf
echo "rpcallow=127.0.0.1" >> coinyecoin.conf
echo "rpcallowip=127.0.0.1" >> coinyecoin.conf
echo "rpcconnect=127.0.0.1" >> coinyecoin.conf
echo "rpcuser=coinyecoinrpc" >> coinyecoin.conf
echo "rpcpassword="$(openssl rand -base64 36) >> coinyecoin.conf
curl http://coinye.planetburt.com/schmapi/nodes.php >> coinyecoin.conf
read -p "Manually create .coinyecoin folder and move over conf file? Y/N?  " -n 1 -r
if [[ ! $REPLY =~ ^[Nn]$ ]]
then
   mkdir /home/$(whoami)/.coinyecoin/
   cp -p coinyecoin.conf /home/$(whoami)/.coinyecoin/
fi
echo
echo "Config file build complete."
cd $loc
