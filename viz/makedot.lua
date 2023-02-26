local sqlite3 = require("lsqlite3")
local graphviz = require("graphviz")

local db = assert( sqlite3.open("./tracer-default.db") )

local streams = { }
local lastStream = 0

local graph = graphviz()
for row in db:nrows("SELECT * FROM Events ORDER BY Id ASC") do
    print(row.Id, row.Stream)
    graph:node(row.Id, row.Name)

    if (streams[row.Stream]) then
        graph:edge(streams[row.Stream], row.Id)
        streams[row.Stream] = row.Id
    else 
        streams[row.Stream] = row.Id 
    end

    lastStream = row.Stream
end


graph.nodes.style:update{
    shape = "rectangle"
}
graph:write("./tracer-default.dot")
