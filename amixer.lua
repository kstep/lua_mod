assert(package.loadlib("./lamixer.so", "luaopen_amixer"))()


hw1 = amixer.open("hw:1")
master = hw1["Master"]
capture = hw1["Capture"]
print(hw1)
print(master)
print(capture)

--print("volume", pcm.vol)
--print("dB", pcm.dB)
--print("muted", pcm.muted)

--print()
--pcm.dB = -450
--pcm.muted = not pcm.muted

--print()
--print("volume", pcm.vol)
--print("dB", pcm.dB)
--print("muted", pcm.muted)

--print()
--print(hw1, pcm)

--print()
--print("playback", pcm.playback)
--print("mono", pcm.mono)
--range = pcm.volrange
--print("range", range[1], range[2])
--range = pcm.dBrange
--print("dBrange", range[1], range[2])

--print()
--print(hw1:each())

--print()
--elems = {}
--for elem in hw1:each() do
    --print(elem, elem.vol)
    --table.insert(elems, elem)
--end

--print()
--for i, v in ipairs(elems) do
    --print(i, v)
--end

--print()
--fleft = pcm["Front Right"]
--print(fleft)
--print(fleft.vol,fleft.dB,fleft.muted)
--fleft.vol = 10
--fleft.muted = not fleft.muted
--print(fleft.vol,fleft.dB,fleft.muted)
