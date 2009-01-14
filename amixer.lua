assert(package.loadlib("./lamixer.so", "luaopen_amixer"))()

--[[

mixer.open(num) => open "hw:<num>" device, returns mixer object,
mixer.close(mix) => close this device,

mix["name"] or mix[num] => set/get value object of this control,
value:both(num) => set volume for both channels of value object,
value[num] => set/get volume for given channel of value object,
value:device() => returns device number of the value 
mix:mixer() => returns mixer object of this device,
mix:device() => returns 
mix:name() => 

--]]

hw1 = amixer.open("hw:0")
pcm = hw1["Digital"]
print("volume", pcm.vol)
print("dB", pcm.dB)
print("muted", pcm.muted)

pcm.dB = -450
pcm.muted = not pcm.muted

print("volume", pcm.vol)
print("dB", pcm.dB)
print("muted", pcm.muted)

print(hw1, pcm)

print("playback", pcm.playback)
print("mono", pcm.mono)
print("joined", pcm.joined)
range = pcm.volrange
print("range", range[1], range[2])
range = pcm.dBrange
print("dBrange", range[1], range[2])
