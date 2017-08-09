buffer = ""
mutex = 0

function Begin()
   WifiConnect()
end

function Listen()
    uart.setup(0, 115200, 8, uart.PARITY_NONE, uart.STOPBITS_1, 1)
    uart.on("data", "\b", DataIn, 0)
    print("Now listening")
end

function DataIn(data)
    buffer = buffer .. data
    if(string.len(data)==256) then -- edge case
        if(string.byte(data,256)=="\b") then
            PublishBuffer()
        end
    else
        PublishBuffer()
    end
    data = nil
end

function PublishBuffer()
    print("Sending data, len ="..string.len(buffer))
  if(isconnected==1) then
    if(mutex==0) then
       mutex = 1
    http.post("http://CHANGEME",'Content-Type: text/xml\r\n',buffer,
  function(code, data)
    if (code < 0) then
      print("HTTP request failed")
    else
        buffer = ""
      --print(code)
    end
    mutex=0
  end)
  end
  end
    
end
