
package.loadlib("./lmpdc.so", "luaopen_mpdc")()

function print_table(tblname, tbl)
	for k,v in pairs(tbl) do
		if type(v) == "table" then
			print_table(tblname .. "[" .. k .. "]", v)
		else
			print(tblname .. "[" .. k .. "]=\"" .. v .. "\"")
		end
	end
end
function print_itable(tblname, tbl)
	for k,v in ipairs(tbl) do
		if type(v) == "table" then
			print_itable(tblname .. "[" .. k .. "]", v)
		else
			print(tblname .. "[" .. k .. "]=\"" .. v .. "\"")
		end
	end
end

mpd = mpdc.open("127.0.0.1", 6600)
--[[
print_table("currentsong", mpd:currentsong())
print()
print_table("status", mpd:status())
print()
print_itable("listall", mpd:listall())
print()
]]
print_table("playlistid", mpd:playlistid())
print()

mpd:reconnect()
print_table("status", mpd:status())
print()

--[[
print(mpd:pause(1))
print(mpd:toggle())
]]

