
#ifndef STX_SMALL_ANY_HPP_INCLUDED
#define STX_SMALL_ANY_HPP_INCLUDED

#ifndef STX_NAMESPACE_NAME
#define STX_NAMESPACE_NAME hpp
#endif

#include <version>

#include <new>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

namespace STX_NAMESPACE_NAME
{

template <size_t StaticCapacity = 2 * sizeof(void*)>
class small_any
{
	using stack_storage_t = typename std::aligned_storage<StaticCapacity, alignof(void*)>::type;
	union storage_union
	{
		void* dynamic;
		stack_storage_t stack;
	};

	/// Whether the type T must be dynamically allocated or can be stored on the stack.
	template <typename T>
	struct requires_allocation_decayed
		: std::integral_constant<bool,
								 !(std::is_nothrow_move_constructible<T>::value && // N4562 §6.3/3 [any.class]
								   sizeof(T) <= sizeof(stack_storage_t) &&
								   alignof(T) <= alignof(stack_storage_t))>
	{
	};

	template <typename T>
	struct requires_allocation : requires_allocation_decayed<typename std::decay<T>::type>
	{
	};

public:
	/// Constructs an object of type any with an empty state.
	small_any()
		: vtable(nullptr)
	{
	}

	/// Constructs an object of type any with an equivalent state as other.
	small_any(const small_any& rhs)
		: vtable(rhs.vtable)
	{
		if(!rhs.empty())
		{
			rhs.vtable->copy(rhs.storage, this->storage);
		}
	}

	/// Constructs an object of type any with a state equivalent to the original state of other.
	/// rhs is left in a valid but otherwise unspecified state.
	small_any(small_any&& rhs) noexcept
		: vtable(rhs.vtable)
	{
		if(!rhs.empty())
		{
			rhs.vtable->move(rhs.storage, this->storage);
			rhs.vtable = nullptr;
		}
	}

	/// Same effect as this->clear().
	~small_any()
	{
		this->clear();
	}

	/// Constructs an object of type any that contains an object of type T direct-initialized with
	/// std::forward<ValueType>(value).
	///
	/// T shall satisfy the CopyConstructible requirements, otherwise the program is ill-formed.
	/// This is because an `any` may be copy constructed into another `any` at any time, so a copy should
	/// always be allowed.
	template <typename ValueType, typename = typename std::enable_if<!std::is_same<
									  typename std::decay<ValueType>::type, small_any>::value>::type>
	small_any(ValueType&& value)
	{
		static_assert(std::is_copy_constructible<typename std::decay<ValueType>::type>::value,
					  "T shall satisfy the CopyConstructible requirements.");
		this->construct(std::forward<ValueType>(value));
	}

	/// Has the same effect as any(rhs).swap(*this). No effects if an exception is thrown.
	small_any& operator=(const small_any& rhs)
	{
		small_any(rhs).swap(*this);
		return *this;
	}

	/// Has the same effect as any(std::move(rhs)).swap(*this).
	///
	/// The state of *this is equivalent to the original state of rhs and rhs is left in a valid
	/// but otherwise unspecified state.
	small_any& operator=(small_any&& rhs) noexcept
	{
		small_any(std::move(rhs)).swap(*this);
		return *this;
	}

	/// Has the same effect as any(std::forward<ValueType>(value)).swap(*this). No effect if a exception is
	/// thrown.
	///
	/// T shall satisfy the CopyConstructible requirements, otherwise the program is ill-formed.
	/// This is because an `any` may be copy constructed into another `any` at any time, so a copy should
	/// always be allowed.
	template <typename ValueType, typename = typename std::enable_if<!std::is_same<
									  typename std::decay<ValueType>::type, small_any>::value>::type>
	small_any& operator=(ValueType&& value)
	{
		static_assert(std::is_copy_constructible<typename std::decay<ValueType>::type>::value,
					  "T shall satisfy the CopyConstructible requirements.");
		small_any(std::forward<ValueType>(value)).swap(*this);
		return *this;
	}

	/// If not empty, destroys the contained object.
	void clear() noexcept
	{
		if(!empty())
		{
			this->vtable->destroy(storage);
			this->vtable = nullptr;
		}
	}

	/// Returns true if *this has no contained object, otherwise false.
	bool empty() const noexcept
	{
		return this->vtable == nullptr;
	}

	bool dynamic() const noexcept
	{
		return !empty() && this->vtable->dynamic();
	}

	/// If *this has a contained object of type T, typeid(T); otherwise typeid(void).
	const std::type_info& type() const noexcept
	{
		return empty() ? typeid(void) : this->vtable->type();
	}

	/// Exchange the states of *this and rhs.
	void swap(small_any& rhs) noexcept
	{
		if(this->vtable != rhs.vtable)
		{
			small_any tmp(std::move(rhs));

			// move from *this to rhs.
			rhs.vtable = this->vtable;
			if(this->vtable != nullptr)
			{
				this->vtable->move(this->storage, rhs.storage);
				// this->vtable = nullptr; -- uneeded, see below
			}

			// move from tmp (previously rhs) to *this.
			this->vtable = tmp.vtable;
			if(tmp.vtable != nullptr)
			{
				tmp.vtable->move(tmp.storage, this->storage);
				tmp.vtable = nullptr;
			}
		}
		else // same types
		{
			if(this->vtable != nullptr)
				this->vtable->swap(this->storage, rhs.storage);
		}
	}

	/// Same effect as is_same(this->type(), t);
	bool is_typed(const std::type_info& t) const
	{
		return is_same(this->type(), t);
	}

	/// Casts (with no type_info checks) the storage pointer as const T*.
	template <typename T>
	const T* cast() const noexcept
	{
		return dynamic() ? reinterpret_cast<const T*>(storage.dynamic)
						 : reinterpret_cast<const T*>(&storage.stack);
	}

	/// Casts (with no type_info checks) the storage pointer as T*.
	template <typename T>
	T* cast() noexcept
	{
		return dynamic() ? reinterpret_cast<T*>(storage.dynamic) : reinterpret_cast<T*>(&storage.stack);
	}

private: // Storage and Virtual Method Table
	/// Base VTable specification.
	struct vtable_type
	{
		// Note: The caller is responssible for doing .vtable = nullptr after destructful operations
		// such as destroy() and/or move().

		/// The type of the object this vtable is for.
		const std::type_info& (*type)() noexcept;

		bool (*dynamic)() noexcept;

		/// Destroys the object in the union.
		/// The state of the union after this call is unspecified, caller must ensure not to use src anymore.
		void (*destroy)(storage_union&) noexcept;

		/// Copies the **inner** content of the src union into the yet unitialized dest union.
		/// As such, both inner objects will have the same state, but on separate memory locations.
		void (*copy)(const storage_union& src, storage_union& dest);

		/// Moves the storage from src to the yet unitialized dest union.
		/// The state of src after this call is unspecified, caller must ensure not to use src anymore.
		void (*move)(storage_union& src, storage_union& dest) noexcept;

		/// Exchanges the storage between lhs and rhs.
		void (*swap)(storage_union& lhs, storage_union& rhs) noexcept;
	};

	/// VTable for dynamically allocated storage.
	template <typename T>
	struct vtable_dynamic
	{
		static const std::type_info& type() noexcept
		{
			return typeid(T);
		}

		static bool dynamic() noexcept
		{
			return true;
		}

		static void destroy(storage_union& storage) noexcept
		{
			// assert(reinterpret_cast<T*>(storage.dynamic));
			delete reinterpret_cast<T*>(storage.dynamic);
		}

		static void copy(const storage_union& src, storage_union& dest)
		{
			dest.dynamic = new T(*reinterpret_cast<const T*>(src.dynamic));
		}

		static void move(storage_union& src, storage_union& dest) noexcept
		{
			dest.dynamic = src.dynamic;
			src.dynamic = nullptr;
		}

		static void swap(storage_union& lhs, storage_union& rhs) noexcept
		{
			// just exchage the storage pointers.
			std::swap(lhs.dynamic, rhs.dynamic);
		}
	};

	/// VTable for stack allocated storage.
	template <typename T>
	struct vtable_stack
	{
		static const std::type_info& type() noexcept
		{
			return typeid(T);
		}

		static bool dynamic() noexcept
		{
			return false;
		}

		static void destroy(storage_union& storage) noexcept
		{
			reinterpret_cast<T*>(&storage.stack)->~T();
		}

		static void copy(const storage_union& src, storage_union& dest)
		{
			new(&dest.stack) T(reinterpret_cast<const T&>(src.stack));
		}

		static void move(storage_union& src, storage_union& dest) noexcept
		{
			// one of the conditions for using vtable_stack is a nothrow move constructor,
			// so this move constructor will never throw a exception.
			new(&dest.stack) T(std::move(reinterpret_cast<T&>(src.stack)));
			destroy(src);
		}

		static void swap(storage_union& lhs, storage_union& rhs) noexcept
		{
			std::swap(reinterpret_cast<T&>(lhs.stack), reinterpret_cast<T&>(rhs.stack));
		}
	};

	/// Returns the pointer to the vtable of the type T.
	template <typename T>
	static vtable_type* vtable_for_type()
	{
		using VTableType = typename std::conditional<requires_allocation<T>::value, vtable_dynamic<T>,
													 vtable_stack<T>>::type;
		static vtable_type table = {
			VTableType::type, VTableType::dynamic, VTableType::destroy,
			VTableType::copy, VTableType::move,	   VTableType::swap,
		};
		return &table;
	}

private:
	/// Checks if two type infos are the same.
	///
	/// If ANY_IMPL_FAST_TYPE_INFO_COMPARE is defined, checks only the address of the
	/// type infos, otherwise does an actual comparision. Checking addresses is
	/// only a valid approach when there's no interaction with outside sources
	/// (other shared libraries and such).
	static bool is_same(const std::type_info& a, const std::type_info& b)
	{
#ifdef ANY_IMPL_FAST_TYPE_INFO_COMPARE
		return &a == &b;
#else
		return a == b;
#endif
	}

	storage_union storage; // on offset(0) so no padding for align
	vtable_type* vtable;

	/// Chooses between stack and dynamic allocation for the type decay_t<ValueType>,
	/// assigns the correct vtable, and constructs the object on our storage.
	template <typename ValueType>
	void construct(ValueType&& value)
	{
		using T = typename std::decay<ValueType>::type;

		this->vtable = vtable_for_type<T>();

		construct_storage(std::forward<ValueType>(value));
	}

	template <typename ValueType>
	std::enable_if_t<requires_allocation<ValueType>::value> construct_storage(ValueType&& value)
	{
		using T = typename std::decay<ValueType>::type;
		storage.dynamic = new T(std::forward<ValueType>(value));
	}

	template <typename ValueType>
	std::enable_if_t<!requires_allocation<ValueType>::value> construct_storage(ValueType&& value)
	{
		using T = typename std::decay<ValueType>::type;
		new(&storage.stack) T(std::forward<ValueType>(value));
	}
};

} // namespace STX_NAMESPACE_NAME

namespace std
{
template <size_t StaticCapacity>
inline void swap(STX_NAMESPACE_NAME::small_any<StaticCapacity>& lhs,
				 STX_NAMESPACE_NAME::small_any<StaticCapacity>& rhs) noexcept
{
	lhs.swap(rhs);
}
} // namespace std

#endif //  STX_SMALL_ANY_HPP_INCLUDED
