-- | A persistent directed graph over hashed vertices.
--
-- Successors and predecessors are both indexed, so neither direction is a scan. Every operation
-- returns a new version and leaves its inputs valid, sharing unchanged structure, so an edit
-- copies a path rather than the whole collection.
module Durable7.Hamt.PersistentDirectedGraph
  ( PersistentDirectedGraph
  , empty
  , emptyWith
  , fromLists
  , vertexCount
  , edgeCount
  , null
  , policy
  , memberVertex
  , memberEdge
  , actualVertex
  , successors
  , predecessors
  , outDegree
  , inDegree
  , insertVertex
  , insertEdge
  , deleteEdge
  , deleteVertex
  , clear
  , reverse
  , vertices
  , edges
  , validStructure
  ) where

import Prelude hiding (null, reverse)

import qualified Data.List as List
import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashPolicy)
import qualified Durable7.Hamt.HashMap as HashMap
import Durable7.Hamt.HashSet (HashSet)
import qualified Durable7.Hamt.HashSet as HashSet
import Durable7.Hamt.Relation (Relation)
import qualified Durable7.Hamt.Relation as Relation

-- | A persistent directed graph over hashed vertices. Successors and predecessors are both indexed,
-- so neither direction is a scan.
data PersistentDirectedGraph v = PersistentDirectedGraph !(HashSet v) !(Relation v v)

-- | The empty graph.
empty :: (Eq v, Hashable v) => PersistentDirectedGraph v
empty = emptyWith HashMap.defaultPolicy

-- | The empty graph under the given policy, which it retains.
emptyWith :: HashPolicy v -> PersistentDirectedGraph v
emptyWith verticesPolicy =
  PersistentDirectedGraph (HashSet.emptyWith verticesPolicy) (Relation.emptyWith verticesPolicy verticesPolicy)

-- | A graph holding the given vertices and edges.
fromLists :: HashPolicy v -> [v] -> [(v, v)] -> PersistentDirectedGraph v
fromLists verticesPolicy initialVertices initialEdges =
  List.foldl' (\graph (source, target) -> insertEdge source target graph)
    (List.foldl' (flip insertVertex) (emptyWith verticesPolicy) initialVertices)
    initialEdges

-- | Number of vertices.
vertexCount :: PersistentDirectedGraph v -> Int
vertexCount (PersistentDirectedGraph storedVertices _) = HashSet.size storedVertices

-- | Number of directed edges.
edgeCount :: PersistentDirectedGraph v -> Int
edgeCount (PersistentDirectedGraph _ storedEdges) = Relation.size storedEdges

-- | Whether the graph holds no vertices.
null :: PersistentDirectedGraph v -> Bool
null graph = vertexCount graph == 0

-- | The policy the graph retains.
policy :: PersistentDirectedGraph v -> HashPolicy v
policy (PersistentDirectedGraph storedVertices _) = HashSet.policy storedVertices

-- | Whether the vertex is present.
memberVertex :: v -> PersistentDirectedGraph v -> Bool
memberVertex vertex (PersistentDirectedGraph storedVertices _) = HashSet.member vertex storedVertices

-- | Whether the directed edge is present.
memberEdge :: v -> v -> PersistentDirectedGraph v -> Bool
memberEdge source target (PersistentDirectedGraph _ storedEdges) = Relation.member source target storedEdges

-- | The stored vertex representative.
actualVertex :: v -> PersistentDirectedGraph v -> Maybe v
actualVertex vertex (PersistentDirectedGraph storedVertices _) = HashSet.actualValue vertex storedVertices

-- | The vertices the given vertex points at.
successors :: v -> PersistentDirectedGraph v -> HashSet v
successors source graph@(PersistentDirectedGraph _ storedEdges) =
  maybe (HashSet.emptyWith (policy graph)) id (Relation.lookupRights source storedEdges)

-- | The vertices pointing at the given vertex. Maintained as its own index, so this is a lookup
-- rather than a scan.
predecessors :: v -> PersistentDirectedGraph v -> HashSet v
predecessors target graph@(PersistentDirectedGraph _ storedEdges) =
  maybe (HashSet.emptyWith (policy graph)) id (Relation.lookupLefts target storedEdges)

-- | How many edges leave the vertex.
outDegree :: v -> PersistentDirectedGraph v -> Int
outDegree source = HashSet.size . successors source

-- | How many edges enter the vertex.
inDegree :: v -> PersistentDirectedGraph v -> Int
inDegree target = HashSet.size . predecessors target

-- | A graph containing the vertex, with no edges added.
insertVertex :: v -> PersistentDirectedGraph v -> PersistentDirectedGraph v
insertVertex vertex graph@(PersistentDirectedGraph storedVertices storedEdges)
  | HashSet.member vertex storedVertices = graph
  | otherwise = PersistentDirectedGraph (HashSet.insert vertex storedVertices) storedEdges

-- | Inserts an edge and implicitly inserts missing endpoint vertices.
insertEdge :: v -> v -> PersistentDirectedGraph v -> PersistentDirectedGraph v
insertEdge source target graph@(PersistentDirectedGraph storedVertices storedEdges)
  | Relation.member source target storedEdges = graph
  | otherwise =
      let withSource = HashSet.insert source storedVertices
          withEndpoints = HashSet.insert target withSource
          actualSource = requiredRepresentative source withEndpoints
          actualTarget = requiredRepresentative target withEndpoints
       in PersistentDirectedGraph withEndpoints (Relation.insert actualSource actualTarget storedEdges)

-- | A graph without that directed edge, leaving both endpoints in place.
deleteEdge :: v -> v -> PersistentDirectedGraph v -> PersistentDirectedGraph v
deleteEdge source target graph@(PersistentDirectedGraph storedVertices storedEdges)
  | not (Relation.member source target storedEdges) = graph
  | otherwise = PersistentDirectedGraph storedVertices (Relation.delete source target storedEdges)

-- | A graph without the vertex and without any edge touching it.
deleteVertex :: v -> PersistentDirectedGraph v -> PersistentDirectedGraph v
deleteVertex vertex graph@(PersistentDirectedGraph storedVertices storedEdges)
  | not (HashSet.member vertex storedVertices) = graph
  | otherwise =
      PersistentDirectedGraph
        (HashSet.delete vertex storedVertices)
        (Relation.deleteRight vertex (Relation.deleteLeft vertex storedEdges))

-- | The empty graph, retaining the same policies.
clear :: PersistentDirectedGraph v -> PersistentDirectedGraph v
clear graph
  | null graph = graph
  | otherwise = emptyWith (policy graph)

-- | The graph in the opposite order.
reverse :: PersistentDirectedGraph v -> PersistentDirectedGraph v
reverse (PersistentDirectedGraph storedVertices storedEdges) =
  PersistentDirectedGraph storedVertices (Relation.inverse storedEdges)

-- | The vertices.
vertices :: PersistentDirectedGraph v -> [v]
vertices (PersistentDirectedGraph storedVertices _) = HashSet.toList storedVertices

-- | The directed edges.
edges :: PersistentDirectedGraph v -> [(v, v)]
edges (PersistentDirectedGraph _ storedEdges) = Relation.toList storedEdges

-- | Whether the graph's structural invariants hold. For tests and diagnostics.
validStructure :: PersistentDirectedGraph v -> Bool
validStructure graph@(PersistentDirectedGraph _ storedEdges) =
  Relation.validStructure storedEdges
    && all (\(source, target) -> memberVertex source graph && memberVertex target graph) (Relation.toList storedEdges)

requiredRepresentative :: v -> HashSet v -> v
requiredRepresentative value values = case HashSet.actualValue value values of
  Just actual -> actual
  Nothing -> error "PersistentDirectedGraph failed to retain an inserted vertex"
