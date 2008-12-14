
package.loadlib("./lsocket.so", "luaopen_socket")()

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

mpd = socket.open("127.0.0.1", 6600)

print(mpd:recv())
mpd:send("status\n")
result = mpd:recv()
print(result)
mpd:send("close\n")
result = mpd:recv()
print(result)

sock = socket.open("mail.ru", 80)

sock:send("GET /\nHost: diary.ru\n\n")

print(sock:recv())
print(sock:recv())
print(sock:recv())
print(sock:recv())
print(sock:recv())
print(sock:recv())
print(sock:recv())
print(sock:recv())
