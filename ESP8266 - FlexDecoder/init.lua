
function bootup()
	dofile("start.lua")
	dofile("network.lua")
	Begin()
end

_, reset_reason = node.bootreason()

if reset_reason == 0 then
    print("Booting in 5 seconds, enter tmr.stop(0) to abort")
     tmr.register(0,5000,tmr.ALARM_SEMI,bootup)
     tmr.start(0)
else 
	print("We crashed..... Reset cause is "..reset_reason)
	print("Booting in 20 seconds, enter tmr.stop(0) to abort")
    tmr.register(0,20000,tmr.ALARM_SEMI,bootup)
    tmr.start(0)
end

reset_reason=nil

