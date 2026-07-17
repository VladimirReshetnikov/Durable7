use std::sync::Arc;

use tools_data_structures_hamt::PersistentDirectedGraph;

#[derive(Clone, Debug)]
struct Vertex {
    class: Arc<str>,
    representation: Arc<str>,
}

impl Vertex {
    fn new(class: &str, representation: &str) -> Self {
        Self {
            class: Arc::from(class),
            representation: Arc::from(representation),
        }
    }
}

impl PartialEq for Vertex {
    fn eq(&self, other: &Self) -> bool {
        self.class.eq_ignore_ascii_case(&other.class)
    }
}
impl Eq for Vertex {}
impl std::hash::Hash for Vertex {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        for byte in self.class.bytes() {
            state.write_u8(byte.to_ascii_lowercase());
        }
    }
}

#[test]
fn edges_add_endpoints_and_bidirectional_adjacency() {
    let graph = PersistentDirectedGraph::new()
        .insert_edge("a", "b")
        .insert_edge("a", "c")
        .insert_edge("c", "a")
        .insert_edge("c", "c");

    assert_eq!(graph.vertex_count(), 3);
    assert_eq!(graph.edge_count(), 4);
    assert_eq!(graph.out_degree(&"c"), 2);
    assert_eq!(graph.in_degree(&"c"), 2);
    assert!(graph.contains_edge(&"c", &"c"));
    assert_eq!(
        graph
            .successors(&"a")
            .unwrap()
            .iter()
            .copied()
            .collect::<std::collections::HashSet<_>>(),
        ["b", "c"].into_iter().collect()
    );
    graph.validate().unwrap();
}

#[test]
fn duplicate_vertices_and_edges_share_roots() {
    let graph = PersistentDirectedGraph::new().insert_edge("source", "target");
    assert!(graph.insert_vertex("source").shares_roots_with(&graph));
    assert!(
        graph
            .insert_edge("source", "target")
            .shares_roots_with(&graph)
    );
    let (same, inserted) = graph.try_insert_edge("source", "target");
    assert!(!inserted);
    assert!(same.shares_roots_with(&graph));
}

#[test]
fn edge_endpoints_use_vertex_set_representatives() {
    let source = Vertex::new("source", "stored-source");
    let target = Vertex::new("target", "stored-target");
    let graph = PersistentDirectedGraph::new()
        .insert_vertex(source.clone())
        .insert_vertex(target.clone())
        .insert_edge(
            Vertex::new("SOURCE", "caller-source"),
            Vertex::new("TARGET", "caller-target"),
        );
    let (actual_source, actual_target) = graph.edges().next().unwrap();

    assert!(Arc::ptr_eq(
        &actual_source.representation,
        &source.representation
    ));
    assert!(Arc::ptr_eq(
        &actual_target.representation,
        &target.representation
    ));
}

#[test]
fn edge_and_vertex_removal_have_distinct_semantics() {
    let source = PersistentDirectedGraph::new()
        .insert_vertex(9)
        .insert_edge(1, 2)
        .insert_edge(2, 1)
        .insert_edge(1, 1)
        .insert_edge(3, 1);
    let without_edge = source.remove_edge(&1, &2);
    let without_one = source.remove_vertex(&1);

    assert!(without_edge.contains_vertex(&1));
    assert!(without_edge.contains_vertex(&2));
    assert!(!without_edge.contains_edge(&1, &2));
    assert_eq!(without_one.edge_count(), 0);
    assert_eq!(
        without_one
            .vertices()
            .copied()
            .collect::<std::collections::HashSet<_>>(),
        [2, 3, 9].into_iter().collect()
    );
    assert_eq!(source.edge_count(), 4);
}

#[test]
fn reversed_is_an_involutive_root_swapping_value() {
    let graph = PersistentDirectedGraph::new()
        .insert_vertex(9)
        .insert_edge(1, 2)
        .insert_edge(3, 2);
    let reversed = graph.reversed();
    let restored = reversed.reversed();

    assert!(reversed.contains_edge(&2, &1));
    assert!(reversed.contains_edge(&2, &3));
    assert!(reversed.contains_vertex(&9));
    assert!(restored.shares_roots_with(&graph));
}

#[test]
fn construction_and_retained_branches_are_independent() {
    let root = PersistentDirectedGraph::from_parts([0], [(1, 2), (2, 3)]);
    let left = root.insert_edge(3, 1);
    let right = root.remove_vertex(&2);

    assert_eq!(root.vertex_count(), 4);
    assert_eq!(root.edge_count(), 2);
    assert_eq!(left.edge_count(), 3);
    assert_eq!(right.edge_count(), 0);
    assert!(root.contains_vertex(&2));
    assert!(!right.contains_vertex(&2));
    root.validate().unwrap();
    left.validate().unwrap();
    right.validate().unwrap();
}
