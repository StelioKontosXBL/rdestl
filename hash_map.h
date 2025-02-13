#ifndef RDESTL_HASH_MAP_H
#define RDESTL_HASH_MAP_H

#include <utility>
#include <tuple> // TODO use own tuple?

#include "pair.h"
#include "algorithm.h"
#include "allocator.h"
#include "functional.h"
#include "rhash.h"
#include "iterator.h"

namespace rde
{

// Load factor is 7/8th.
template<typename TKey, typename TValue,
	class THashFunc		= rde::hash<TKey>,
	class TKeyEqualFunc	= rde::equal_to<TKey>,
	class TAllocator	= rde::allocator
>
class hash_map
{
public:
	typedef rde::pair<TKey, TValue>         value_type;

//private:
	struct node
	{
		static const hash_value_t kUnusedHash       = 0xFFFFFFFF;
		static const hash_value_t kDeletedHash      = 0xFFFFFFFE;

		node(): hash(kUnusedHash) {}

		RDE_FORCEINLINE bool is_unused() const		{ return hash == kUnusedHash; }
		RDE_FORCEINLINE bool is_deleted() const		{ return hash == kDeletedHash; }
		RDE_FORCEINLINE bool is_occupied() const	{ return hash < kDeletedHash; }

		hash_value_t    hash;
		value_type      data;
	};

	template<typename TNodePtr, typename TPtr, typename TRef>
	class node_iterator
	{
		friend class hash_map;
	public:
		typedef forward_iterator_tag    iterator_category;

		explicit node_iterator(TNodePtr node, const hash_map* map)
			: m_node(node),
			m_map(map)
		{
			/**/
		}

		// const/non-const iterator copy ctor
		template<typename UNodePtr, typename UPtr, typename URef>
		node_iterator(const node_iterator<UNodePtr, UPtr, URef>& rhs)
			: m_node(rhs.node()),
			m_map(rhs.get_map())
		{
			 /**/
		}
		TRef operator*() const					{ RDE_ASSERT(m_node != 0); return m_node->data; }
		TPtr operator->() const					{ return &m_node->data; }
		RDE_FORCEINLINE TNodePtr node() const	{ return m_node; }

		node_iterator& operator++()
		{
			RDE_ASSERT(m_node != 0);
			++m_node;
			move_to_next_occupied_node();
			return *this;
		}
		node_iterator operator++(int)
		{
			node_iterator copy(*this);
			++(*this);
			return copy;
		}

		RDE_FORCEINLINE bool operator==(const node_iterator& rhs) const { return rhs.m_node == m_node; }
		RDE_FORCEINLINE bool operator!=(const node_iterator& rhs) const { return !(rhs == *this); }

		const hash_map* get_map() const { return m_map; }

	private:
		void move_to_next_occupied_node()
		{
			// @todo: save nodeEnd in constructor?
			TNodePtr nodeEnd = m_map->m_nodes + m_map->bucket_count();
			for (; m_node < nodeEnd; ++m_node)
			{
				if (m_node->is_occupied())
					break;
			}
		}

		TNodePtr		m_node;
		const hash_map*	m_map;
	};

public:
	typedef TKey																key_type;
	typedef TValue																mapped_type;
	typedef TAllocator															allocator_type;
	typedef node_iterator<node*, value_type*, value_type&>						iterator;
	typedef node_iterator<const node*, const value_type*, const value_type&>	const_iterator;
	typedef size_t																size_type;

	static const size_type														kNodeSize = sizeof(node);
	static const size_type														kInitialCapacity = 64;

	hash_map()
		: m_nodes(&ms_emptyNode),
		m_size(0),
		m_capacity(0),
		m_capacityMask(0),
		m_numUsed(0)
	{
		RDE_ASSERT((kInitialCapacity & (kInitialCapacity - 1)) == 0);	// Must be power-of-two
	}
	explicit hash_map(const allocator_type& allocator)
		: m_nodes(&ms_emptyNode),
		m_size(0),
		m_capacity(0),
		m_capacityMask(0),
		m_numUsed(0),
		m_allocator(allocator)
	{
		/**/
	}
	explicit hash_map(size_type initial_bucket_count, const allocator_type& allocator = allocator_type())
		: m_nodes(&ms_emptyNode),
		m_size(0),
		m_capacity(0),
		m_capacityMask(0),
		m_numUsed(0),
		m_allocator(allocator)
	{
		reserve(initial_bucket_count);
	}
	hash_map(size_type initial_bucket_count, const THashFunc& hashFunc, const allocator_type& allocator = allocator_type())
		: m_nodes(&ms_emptyNode),
		m_size(0),
		m_capacity(0),
		m_capacityMask(0),
		m_numUsed(0),
		m_hashFunc(hashFunc),
		m_allocator(allocator)
	{
		reserve(initial_bucket_count);
	}
	hash_map(const hash_map& rhs, const allocator_type& allocator = allocator_type())
		: m_nodes(&ms_emptyNode),
		m_size(0),
		m_capacity(0),
		m_capacityMask(0),
		m_numUsed(0),
		m_allocator(allocator)
	{
		*this = rhs;
	}
	explicit hash_map(e_noinitialize)
	{
	}
	~hash_map()
	{
		delete_nodes();
	}

	iterator begin()
	{
		iterator it(m_nodes, this);
		it.move_to_next_occupied_node();
		return it;
	}
	const_iterator begin() const
	{
		const_iterator it(m_nodes, this);
		it.move_to_next_occupied_node();
		return it;
	}
	iterator end()              { return iterator(m_nodes + m_capacity, this); }
	const_iterator end() const	{ return const_iterator(m_nodes + m_capacity, this); }

	// @note:	Added for compatiblity sake.
	//			Personally, I consider it "risky". Use find/insert for more
	//			explicit operations.
	mapped_type& operator[](const key_type& key)
	{
		hash_value_t hash;
		node* n = find_for_insert(key, &hash);
		if (n == 0 || !n->is_occupied())
		{
			return insert_at(value_type(key, TValue()), n, hash).first->second;
		}
		return n->data.second;
	}
	// @note:	Doesn't copy allocator.
	hash_map& operator=(const hash_map& rhs)
	{
		RDE_ASSERT(invariant());
		if (&rhs != this)
		{
			clear();
			if (m_capacity < rhs.bucket_count())
			{
				delete_nodes();
				m_nodes = allocate_nodes(rhs.bucket_count());
				m_capacity = rhs.bucket_count();
				m_capacityMask = m_capacity - 1;
			}
			rehash(m_capacity, m_nodes, rhs.m_capacity, rhs.m_nodes, false);
			m_size = rhs.size();
			m_numUsed = rhs.m_numUsed;
		}
		RDE_ASSERT(invariant());
		return *this;
	}
	void swap(hash_map& rhs)
	{
		if (&rhs != this)
		{
			RDE_ASSERT(invariant());
			RDE_ASSERT(m_allocator == rhs.m_allocator);
			rde::swap(m_nodes, rhs.m_nodes);
			rde::swap(m_size, rhs.m_size);
			rde::swap(m_capacity, rhs.m_capacity);
			rde::swap(m_capacityMask, rhs.m_capacityMask);
			rde::swap(m_numUsed, rhs.m_numUsed);
			rde::swap(m_hashFunc, rhs.m_hashFunc);
			rde::swap(m_keyEqualFunc, rhs.m_keyEqualFunc);
			RDE_ASSERT(invariant());
		}
	}

	rde::pair<iterator, bool> insert(const value_type& v)
	{
		return emplace(v.first, v.second);
	}
	template<class K = key_type, class... Args>
	rde::pair<iterator, bool> emplace(K&& key, Args&&... args)
	{
		RDE_ASSERT(invariant());
		if (m_numUsed * 8 >= m_capacity * 7)
			grow();

		hash_value_t hash;
		node* n = find_for_insert(key, &hash);

		return emplace_at(n, hash, std::forward<K>(key), std::forward<Args>(args)...);
	}

	size_type erase(const key_type& key)
	{
		node* n = lookup(key);
		if (n != (m_nodes + m_capacity) && n->is_occupied())
		{
			erase_node(n);
			return 1;
		}
		return 0;
	}
	void erase(iterator it)
	{
		RDE_ASSERT(it.get_map() == this);
		if (it != end())
		{
			RDE_ASSERT(!empty());
			erase_node(it.node());
		}
	}
	void erase(iterator from, iterator to)
	{
		for (; from != to; ++from)
		{
			node* n = from.node();
			if (n->is_occupied())
				erase_node(n);
		}
	}

	iterator find(const key_type& key)
	{
		node* n = lookup(key);
		return iterator(n, this);
	}
	const_iterator find(const key_type& key) const
	{
		const node* n = lookup(key);
		return const_iterator(n, this);
	}

	void clear()
	{
		node* endNode = m_nodes + m_capacity;
		for (node* iter = m_nodes; iter != endNode; ++iter)
		{
			if (iter)
			{
				if (iter->is_occupied())
				{
					rde::destruct(&iter->data);
				}
				// We can make them unused, because we clear whole hash_map,
				// so we can guarantee there'll be no holes.
				iter->hash = node::kUnusedHash;
			}
		}
		m_size = 0;
		m_numUsed = 0;
	}

	void reserve(size_type min_size)
	{
		size_type newCapacity = (m_capacity == 0 ? kInitialCapacity : m_capacity);
		while (newCapacity < min_size)
			newCapacity *= 2;
		if (newCapacity > m_capacity)
			grow(newCapacity);
	}

	size_type bucket_count() const			{ return m_capacity; }
	size_type size() const					{ return m_size; }
	size_type empty() const					{ return size() == 0; }
	size_type nonempty_bucket_count() const	{ return m_numUsed; }
	size_type used_memory() const			{ return bucket_count() * kNodeSize; }

	const allocator_type& get_allocator() const	{ return m_allocator; }
	void set_allocator(const allocator_type& allocator) { m_allocator = allocator; }

private:
	void grow()
	{
		const size_type newCapacity = (m_capacity == 0 ? kInitialCapacity : m_capacity * 2);
		grow(newCapacity);
	}
	void grow(size_t new_capacity)
	{
		RDE_ASSERT((new_capacity & (new_capacity - 1)) == 0);	// Must be power-of-two
		node* newNodes = allocate_nodes(new_capacity);
		rehash(new_capacity, newNodes, m_capacity, m_nodes, true);
		if (m_nodes != &ms_emptyNode)
			m_allocator.deallocate(m_nodes, sizeof(node) * m_capacity);
		m_capacity = new_capacity;
		m_capacityMask = new_capacity - 1;
		m_nodes = newNodes;
		m_numUsed = m_size;
		RDE_ASSERT(m_numUsed < m_capacity);
	}

	template<class K = key_type, class... Args> RDE_FORCEINLINE
	rde::pair<iterator, bool> emplace_at(node* n, hash_value_t hash, K&& key, Args&&... args)
	{
		typedef rde::pair<iterator, bool> ret_type_t;
		if (n->is_occupied())
		{
			RDE_ASSERT(hash == n->hash && m_keyEqualFunc(key, n->data.first));
			return ret_type_t(iterator(n, this), false);
		}
		if (n->is_unused())
		{
			++m_numUsed;
		}
		rde::construct_args(&n->data,
			std::forward<K>(key),
			std::forward<Args>(args)...);
		n->hash = hash;
		++m_size;
		RDE_ASSERT(invariant());
		return ret_type_t(iterator(n, this), true);
	}

	rde::pair<iterator, bool> insert_at(const value_type& v, node* n, hash_value_t hash)
	{
		RDE_ASSERT(invariant());
		if (n == 0 || m_numUsed * 8 >= m_capacity * 7)
			return insert(v);

		RDE_ASSERT(!n->is_occupied());
		return emplace_at(n, hash, v.first, v.second);
	}
	node* find_for_insert(const key_type& key, hash_value_t* out_hash)
	{
		if (m_capacity == 0)
			return 0;

		const hash_value_t hash = hash_func(key);
		*out_hash = hash;
		std::uint32_t i = hash & m_capacityMask;

		node* n = m_nodes + i;
		if (n->hash == hash && m_keyEqualFunc(key, n->data.first))
			return n;

		node* freeNode(0);
		if (n->is_deleted())
			freeNode = n;
		std::uint32_t numProbes(1);
		// Guarantees loop termination.
		RDE_ASSERT(m_numUsed < m_capacity);
		while (!n->is_unused())
		{
			i = (i + numProbes) & m_capacityMask;
			n = m_nodes + i;
			if (compare_key(n, key, hash))
				return n;
			if (n->is_deleted() && freeNode == 0)
				freeNode = n;
			++numProbes;
		}
		return freeNode ? freeNode : n;
	}
	node* lookup(const key_type& key) const
	{
		const hash_value_t hash = hash_func(key);
		std::uint32_t i = hash & m_capacityMask;
		node* n = m_nodes + i;
		if (n->hash == hash && m_keyEqualFunc(key, n->data.first))
			return n;

		std::uint32_t numProbes(1);
		// Guarantees loop termination.
		RDE_ASSERT(m_capacity == 0 || m_numUsed < m_capacity);
		while (!n->is_unused())
		{
			i = (i + numProbes) & m_capacityMask;
			n = m_nodes + i;

			if (compare_key(n, key, hash))
				return n;

			++numProbes;
		}
		return m_nodes + m_capacity;
	}

	static void rehash(size_t new_capacity, node* new_nodes, size_t capacity, const node* nodes, bool destruct_original)
	{
		//if (nodes == &ms_emptyNode || new_nodes == &ms_emptyNode)
		//  return;

		node* it = const_cast<node*>(nodes);
		const node* itEnd = nodes + capacity;
		const std::uint32_t mask = new_capacity - 1;
		while (it != itEnd)
		{
			if (it->is_occupied())
			{
				const hash_value_t hash = it->hash;
				std::uint32_t i = hash & mask;

				node* n = new_nodes + i;
				std::uint32_t numProbes(0);
				while (!n->is_unused())
				{
					++numProbes;
					i = (i + numProbes) & mask;
					n = new_nodes + i;
				}
				// rehash is not inlined, so branch will not be eliminated, even though
				// it's known at compile-time. It should be easily predictable though
				// as it's always true/false for each iteration.
				// an alternative would be to either inline it or make a template argument.
				// Both would bloat the code a bit.
				if (destruct_original)
				{
					rde::construct_args(&n->data, std::move(it->data));
					rde::destruct(&it->data);
				}
				else
				{
					rde::copy_construct(&n->data, it->data);
				}
				n->hash = hash;
			}
			++it;
		}
	}

	node* allocate_nodes(size_t n)
	{
		node* buckets = static_cast<node*>(m_allocator.allocate(n * sizeof(node)));
		node* iterBuckets(buckets);
		node* end = iterBuckets + n;
		for (; iterBuckets != end; ++iterBuckets)
			iterBuckets->hash = node::kUnusedHash;
		return buckets;
	}
	void delete_nodes()
	{
		node* it = m_nodes;
		node* itEnd = it + m_capacity;
		while (it != itEnd)
		{
			if (it && it->is_occupied())
				rde::destruct(&it->data);
			++it;
		}
		if (m_nodes != &ms_emptyNode)
			m_allocator.deallocate(m_nodes, sizeof(node) * m_capacity);

		m_capacity = 0;
		m_capacityMask = 0;
		m_size = 0;
	}
	void erase_node(node* n)
	{
		RDE_ASSERT(!empty());
		RDE_ASSERT(n->is_occupied());
		rde::destruct(&n->data);
		n->hash = node::kDeletedHash;
		--m_size;
	}

	RDE_FORCEINLINE hash_value_t hash_func(const key_type& key) const
	{
		const hash_value_t h = m_hashFunc(key) & 0xFFFFFFFD;
		//RDE_ASSERT(h < node::kDeletedHash);
		return h;
	}
	bool invariant() const
	{
		RDE_ASSERT((m_capacity & (m_capacity - 1)) == 0);
		RDE_ASSERT(m_numUsed >= m_size);
		return true;
	}

	RDE_FORCEINLINE bool compare_key(const node* n, const key_type& key, hash_value_t hash) const
	{
		return (n->hash == hash && m_keyEqualFunc(key, n->data.first));
	}

	node*			m_nodes;
	size_type		m_size;
	size_type		m_capacity;
	std::uint32_t	m_capacityMask;
	size_type		m_numUsed;
	THashFunc       m_hashFunc;
	TKeyEqualFunc	m_keyEqualFunc;
	TAllocator      m_allocator;

	static node		ms_emptyNode;
};

// Holy ...
template<typename TKey, typename TValue,
	class THashFunc,
	class TKeyEqualFunc,
	class TAllocator
>
typename hash_map<TKey, TValue, THashFunc, TKeyEqualFunc, TAllocator>::node hash_map<TKey, TValue, THashFunc, TKeyEqualFunc, TAllocator>::ms_emptyNode;

} // namespace rde

//-----------------------------------------------------------------------------
#endif // #ifndef RDESTL_HASH_MAP_H
