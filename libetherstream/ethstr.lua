local ethstr = Proto("ethstr", "EtherStream");

local vs_pktypes = {
	[0] = "CONNECT",
	[1] = "DATA",
	[2] = "ACK",
	[3] = "CLOSE"
}

local fields = {
	type = ProtoField.uint8("ethstr.type", "Packet type", base.DEC, vs_pktypes),
	conn = ProtoField.uint32("ethstr.conn", "Connection id", base.HEX),
	pktNo = ProtoField.uint16("ethstr.no", "Packet number", base.DEC),	
	service = ProtoField.uint16("ethstr.service", "Service", base.HEX),
	sentCount = ProtoField.uint8("ethstr.sent", "Sent Count", base.DEC),
	chksum = ProtoField.uint16("ethstr.chksum", "Checksum", base.HEX),
	datalen = ProtoField.uint16("ethstr.datalen", "DataLen", base.DEC),
	data = ProtoField.bytes("ethstr.data", "Data", base.HEX),	
	received = ProtoField.uint8("ethstr.recv", "Received count", base.DEC),
	conflags = ProtoField.uint16("ethstr.conflags", "Unused", base.HEX),		
	errflags = ProtoField.uint16("ethstr.errflags", "Error Flags", base.HEX)
}

ethstr.fields = fields

function ethstr.dissector(buf, pinfo, tree)
	local type = buf(0,1):le_uint()
	local pt = tree:add(ethstr, buf(0,8))
	pinfo.cols.protocol = "EtherStream"
	pt:add(fields.type, buf(0,1))
	pt:add_le(fields.conn, buf(1,4))
	pt:add_le(fields.pktNo, buf(5,2))
	if type == 0 then -- CONNECT
		pt:add(fields.sentCount, buf(7,1))		
		pt:add_le(fields.service, buf(8,2))
		pt:add(fields.conflags, buf(10,2))
		pinfo.cols.info = "C->S: CONNECT " .. buf(1,4):le_uint()
	elseif type == 1 then -- DATA
		pt:add_le(fields.sentCount, buf(7,1))
		pt:add_le(fields.chksum, buf(8,2))
		pt:add_le(fields.datalen, buf(10,2))
		pt:add(fields.data, buf(12))
		local no = bit.band(buf(5,2):le_uint(),0x7FFF)
		local dir = bit.band(buf(5,2):le_uint(),0x8000)
		if(dir ~= 0) then
			pinfo.cols.info = "S->C: DATA[" .. no .. "]"
		else		
			pinfo.cols.info = "C->S: DATA[" .. no .. "]"
		end
	elseif type == 2 then -- ACK
		pt:add_le(fields.received, buf(7,1))		
		pt:add_le(fields.errflags, buf(8,2))
		local no = bit.band(buf(5,2):le_uint(),0x7FFF)
		local dir = bit.band(buf(5,2):le_uint(),0x8000)
		if(dir == 0) then
			pinfo.cols.info = "S->C: ACK[" .. no .. "]"
		else		
			pinfo.cols.info = "C->S: ACK[" .. no .. "]"
		end
	elseif type == 3 then -- CLOSE
		local no = bit.band(buf(5,2):le_uint(),0x7FFF)
		local dir = bit.band(buf(5,2):le_uint(),0x8000)
		if(dir == 0) then
			pinfo.cols.info = "S->C: CLOSE[" .. no .. "]"
		else		
			pinfo.cols.info = "C->S: CLOSE[" .. no .. "]"
		end
	else
		-- Error
	end
	
end

local eth_table = DissectorTable.get("ethertype")
eth_table:add(0xFFF0, ethstr)
