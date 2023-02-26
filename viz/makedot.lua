local sqlite3 = require("lsqlite3")
local graphviz = require("graphviz")

local db = assert( sqlite3.open("./kripke.db") )

local streams = { }

local graph = graphviz()
local lastStream = -1
local lastnode = nil

local nodes = {}
local edges = {}
for row in db:nrows("SELECT * FROM Events ORDER BY Id ASC") do
    -- print(row.Id, row.Name)
    graph:node(row.Id, row.Name .. " ID:" .. row.Id)

    -- local edge = {}
    -- if streams[row.Stream] then
    --     edge.start = streams[row.Stream]
    --     edge.finish = node
    -- elseif lastnode then
    --     edge.start = lastnode
    --     edge.finish = node
    -- end

    -- if edge.start then
    --     local prev_edge = edges[edge.start .. edge.finish]
    --     if prev_edge then
    --         prev_edge.count = prev_edge.count + 1
    --     else
    --         edge.count = 1
    --         edges[edge.start .. edge.finish] = edge
    --     end
    -- end

    -- lastnode = node
    if (streams[row.Stream]) then
        graph:edge(streams[row.Stream], row.Id) 
    else 
        if lastStream ~= -1 then
            graph:edge(streams[lastStream], row.Id)
        end
    end
    streams[row.Stream] = row.Id  

    lastStream = row.Stream
    -- if streams[row.Stream] then
    --     nodes = streams[row.Stream]
    --     if nodes[row.Name] then
    --         if lastPerStream[row.Stream] then
    --             graph:edge(lastPerStream[row.Stream], row.Name)
    --         end
    --     else
    --         nodes[row.Name] = graph:node(row.Name, row.Name)
    --     end
    -- else
    --     streams[row.Stream] = {}
    -- end
    -- lastPerStream[row.Stream] = row.Name
end

-- for label, obj in pairs(edges) do
--     print(obj.start, obj.finish, obj.count)
--     graph:edge(obj.count, obj.start, obj.finish)
-- end

graph.nodes.style:update{
    shape = "rectangle"
}
graph:write("./tracer-default.dot")


