function is_eq(value, expected, message)
	io.write(message .. '...\t')
	assert(value == expected, 'failed: expected ' .. tostring(expected) .. ', got ' .. tostring(value))
	print('ok')
end

function is_ok(value, message)
	io.write(message .. '...\t')
	assert(value, 'failed')
	print('ok')
end



-- load lib
assert(package.loadlib("./lmixer4.so", "luaopen_mixer"))()
is_ok(mixer, 'Load mixer module')

-- open mixer
mix = mixer.open(0)
is_ok(mix, 'Open mixer device')

-- mixer's len
is_eq(#mix, 82, 'Get mixer extensions number')

for i = 1, #mix do
    value = mix[i].value
    if type(value) == 'table' then
	is_eq(mix[i][1], value[1], '')
	is_eq(mix[i][2], value[2], '')
    end
    if mix[i].type == 'enum' then
    for k,v in ipairs(mix[i].enum) do
	print(k,v)
    end
end

    print(
	i,
	mix[i].name, mix[i].id,
	mix[i].type,
	#mix[i],
	mix[i].readonly,
	type(value) == 'table' and tostring(value[1])..':'..tostring(value[2]) or value,
	mix[i].enum,
	mix[i].min,
	mix[i].max
	)
    print(
	mix[i][1], mix[i][2],
	mix[i].type == 'enum' and table.concat(mix[i].enum, "|") or "")
end

jack = mix['jack.int-purple']
is_ok(jack, 'Get extension object')

is_eq(jack.type, "group", 'Get extension type')

