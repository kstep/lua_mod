assert(package.loadlib("./lmixer.so", "luaopen_mixer"))()

function print_vol(mx)
	local vol = mx["vol"]
	local remix = vol:mixer()
	print(vol,vol[1],vol[2])
	print(remix)
end

print("test: mixer 0 opening")
mix = mixer.open(1)

print("\ntest: printing channels: pcm[1], vol[2]")
print(mix["pcm"][1])
print(mix["digital1"][1])

--[[print("\ntest: passing mixer to function")
print_vol(mix)

print("\ntest: get channel into separate var and print it")
pcm = mix["pcm"]
print(pcm)

print("\ntest: force GC to run")
collectgarbage("collect")

print("\ntest: redefine mix variable to non-mixer object")
mix = ""

print("\ntest: get mixer from separate channel and print it")
remix = pcm:mixer()
print(remix)

print("\ntest: garbage should be collected now!\n")
]]
