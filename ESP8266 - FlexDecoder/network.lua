isconnected = 0

function WifiConnect()
    StopWifi()
    wifi.setmode(wifi.STATION)
    wifi.sta.eventMonReg(wifi.STA_WRONGPWD, function() ConnectionFailed("Wrong AP Key") end)
    wifi.sta.eventMonReg(wifi.STA_APNOTFOUND, function() ConnectionFailed("AP not found") end)
    wifi.sta.eventMonReg(wifi.STA_FAIL, function() ConnectionFailed("Dunno, failed") end)
    wifi.eventmon.register(wifi.eventmon.STA_CONNECTED, IsConnected)
    wifi.eventmon.register(wifi.eventmon.STA_GOT_IP, IpReceived)
    wifi.eventmon.register(wifi.eventmon.STA_DHCP_TIMEOUT, NoIpReceived)
    wifi.sta.eventMonStart()
    wifi.sta.sethostname("P2000Feed")
    wifi.sta.config("<SSID_HERE>","<KEY_HERE>", 0)
    wifi.sta.connect()

end

function StopWifi()
    wifi.eventmon.unregister(wifi.eventmon.STA_CONNECTED)
    wifi.eventmon.unregister(wifi.eventmon.STA_DISCONNECTED)
    wifi.eventmon.unregister(wifi.eventmon.STA_GOT_IP)
    wifi.eventmon.unregister(wifi.eventmon.STA_DHCP_TIMEOUT)
    wifi.sta.eventMonStop(1)
    wifi.sta.disconnect()
end


function IsConnected(t)
    temp = t.BSSID.."/"..t.channel
    print("\n\tSTA is now CONNECTED".."\n\tSSID: "..t.SSID.."\n\tBSSID: "..t.BSSID.."\n\tChannel: "..t.channel)
    wifi.eventmon.register(wifi.eventmon.STA_DISCONNECTED, isDisconnected)
end

function IsDisconnected(t)
    temp = "Reason: "..t.reason
    print("\n\tSTA - DISCONNECTED".."\n\tSSID: "..t.SSID.."\n\tBSSID: "..t.BSSID.."\n\treason: "..t.reason)
    tmr.register(2,1000,tmr.ALARM_SEMI,WifiConnect)
    tmr.start(2)
    isconnected = 0
end

function ConnectionFailed(reason)
    temp = reason
    tmr.register(2,5000,tmr.ALARM_SEMI,WifiConnect)
    tmr.start(2)
    isconnected = 0
end

function IpReceived(t)
    temp = "IP: "..t.IP
    print("\n\tSTA - GOT IP".."\n\tStation IP: "..t.IP)
    isconnected = 1
    Listen()
end

function NoIpReceived(t)
    print("Timeout waiting for DHCP")    
    tmr.register(2,5000,tmr.ALARM_SEMI,WifiConnect)
    tmr.start(2)
end
