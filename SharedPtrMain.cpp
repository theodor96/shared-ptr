#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>


namespace
{
    template <typename DataT>
    void* convertToVoidPtr(DataT* data)
    {
        if (!data)
        {
            throw std::logic_error{"SharedPtrDataManagementTable method called with null data"};
        }

        return static_cast<void*>(data);
    }
}


class SharedPtrDataManagementTable
{
public:
    static auto& GetInstance()
    {
        static SharedPtrDataManagementTable instance{};
        return instance;
    }

    template <typename DataT>
    void addData(DataT* data)
    {
        auto* voidData = convertToVoidPtr(data);

        const auto findItr = m_managementTable.find(voidData);
        if (findItr == m_managementTable.end())
        {
            m_managementTable.emplace(voidData, 1);
        }
        else
        {
            ++findItr->second;
        }
    }

    template <typename DataT>
    bool removeData(DataT* data)
    {
        const auto findItr = m_managementTable.find(convertToVoidPtr(data));
        if (findItr == m_managementTable.end())
        {
            throw std::logic_error{"SharedPtrDataManagementTable::removeData called with non-managed data"};
        }

        if (findItr->second.load() > 1)
        {
            --findItr->second;
            return false;
        }

        m_managementTable.erase(findItr);
        return true;
    }

    template <typename DataT>
    std::size_t getCount(DataT* data) const
    {
        const auto findItr = m_managementTable.find(convertToVoidPtr(data));
        if (findItr == m_managementTable.cend())
        {
            return 0;
        }

        return findItr->second.load();
    }

private:
    using ManagementTable = std::unordered_map<void*, std::atomic_size_t>;
    ManagementTable m_managementTable;

    SharedPtrDataManagementTable() = default;
};


template <typename DataT>
class SharedPtr
{
public:
    using Data = DataT;

    explicit SharedPtr(DataT* data)
    : m_data{data},
      m_managementTableRef{SharedPtrDataManagementTable::GetInstance()}
    {
        if (m_data)
        {
            m_managementTableRef.addData(m_data);
        }
    }
    
    SharedPtr()
    : SharedPtr{static_cast<DataT*>(nullptr)}
    {
        
    }

    explicit SharedPtr(std::nullptr_t)
    : SharedPtr{}
    {

    }

    SharedPtr(const SharedPtr<DataT>& other)
    : SharedPtr{other.m_data}
    {

    }

    SharedPtr(SharedPtr<DataT>&& other) noexcept
    : m_data{other.m_data},
      m_managementTableRef{SharedPtrDataManagementTable::GetInstance()}
    {
        other.m_data = nullptr;
    }

    template <typename DataU, typename = std::enable_if_t<std::is_base_of_v<DataT, DataU> ||
                                                          std::is_base_of_v<DataU, DataT>>>
    SharedPtr(DataU* data)
    : SharedPtr{(std::is_base_of_v<DataT, DataU>) ? static_cast<DataT*>(data) 
                                                  : dynamic_cast<DataT*>(data)}
    {

    }

    template <typename DataU>
    SharedPtr(const SharedPtr<DataU>& other)
    : SharedPtr{other.m_data}
    {

    }

    SharedPtr<DataT>& operator=(const SharedPtr<DataT>& other)
    {
        assignSelf(other);
        return *this;
    }

    SharedPtr<DataT>& operator=(SharedPtr<DataT>&& other) noexcept
    {
        assignSelf(std::move(other));
        return *this;
    }

    template <typename DataU>
    SharedPtr<DataT>& operator=(const SharedPtr<DataU>& other)
    {
        assignSelf(other);
        return *this;
    }

    template <typename DataU>
    SharedPtr<DataT>& operator=(SharedPtr<DataU>&& other) noexcept
    {
        assignSelf(std::move(other));
        return *this;
    }

    ~SharedPtr()
    {
        releaseData(true);
    }

    void release()
    {
        releaseData(false);
    }

    DataT* operator->()
    {
        throwIfInvalidAccess();

        return m_data;
    }

    const DataT* operator->() const
    {
        return const_cast<SharedPtr<DataT>&>(*this).operator->();
    }

    DataT& operator*()
    {
        throwIfInvalidAccess();

        return *m_data;
    }

    const DataT& operator*() const
    {
        return *const_cast<SharedPtr<DataT>&>(*this);
    }

    operator bool() const
    {
        return m_data;
    }

    std::size_t getUseCount() const
    {
        if (!m_data)
        {
            return 0;
        }

        return m_managementTableRef.getCount(m_data);
    }

    template<typename> friend class SharedPtr;

private:
    DataT* m_data;
    SharedPtrDataManagementTable& m_managementTableRef;

    template <typename SharedPtrT>
    void assignSelf(SharedPtrT&& other)
    {
        using DataU = typename std::remove_reference_t<std::remove_cv_t<SharedPtrT>>::Data;

        static_assert(std::is_base_of_v<DataT, DataU>,
                      "SharedPtr may only be assigned to SharedPtr<derived from this one's DataT>");

        if constexpr (std::is_same_v<DataT, DataU>)
        {
            if (this == &other)
            {
                return;
            }
        }

        releaseData(true);

        m_data = other.m_data;
        assignSelfContinuation(std::forward<SharedPtrT>(other));
    }

    template <typename DataU>
    void assignSelfContinuation(const SharedPtr<DataU>&)
    {
        m_managementTableRef.addData(m_data);
    }

    template <typename DataU>
    void assignSelfContinuation(SharedPtr<DataU>&& other)
    {
        other.m_data = nullptr;
    }

    void releaseData(bool deleteIfLast)
    {
        if (m_data && m_managementTableRef.removeData(m_data) && deleteIfLast)
        {
            delete m_data;
        }

        m_data = nullptr;
    }

    void throwIfInvalidAccess() const
    {
        if (!m_data)
        {
            throw std::logic_error{"SharedPtr dereferenced with null managed data"};
        }
    }
};


template <typename DataT, typename... ArgsT>
SharedPtr<DataT> MakeSharedPtr(ArgsT&&... args)
{
    return SharedPtr<DataT>{new DataT{std::forward<ArgsT>(args)...}};
}


class Base
{
public:
    explicit Base(std::string description)
    : m_data{new int{10}},
      m_instanceIndex{++m_classIndex},
      m_description{std::move(description)}
    {
        ++m_aliveInstanceCount;

        std::cout << "Base::Base(): instance #" << m_instanceIndex 
                  << " constructed!\n" << std::flush;
    }

    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    Base(Base&&) = delete;
    Base& operator=(Base&&) = delete;

    virtual ~Base()
    {
        delete m_data;

        --m_aliveInstanceCount;

        std::cout << "Base::~Base(): instance #" << m_instanceIndex 
                  << " destroyed...\n" << std::flush;
    }

    virtual void showDescription() const
    {
        std::cout << "instance #" << m_instanceIndex 
                  << " with description = " << m_description << "\n" << std::flush;
    }

    static int getCountOfAliveInstances()
    {
        return m_aliveInstanceCount;
    }

private:
    static int m_aliveInstanceCount;
    static int m_classIndex;

    const int* m_data;
    const int m_instanceIndex;
    const std::string m_description;
};


int Base::m_aliveInstanceCount{};
int Base::m_classIndex{};


class Derived : public Base
{
public:
    explicit Derived(std::string description)
    : Base{std::move(description)},
      m_otherData{new double{10.5}}
    {
        std::cout << "Derived::Derived()\n" << std::flush;
    }

    Derived(const Derived&) = delete;
    Derived& operator=(const Derived&) = delete;
    Derived(Derived&&) = delete;
    Derived& operator=(Derived&&) = delete;

    ~Derived()
    {
        std::cout << "Derived::~Derived()\n" << std::flush;
        delete m_otherData;
    }

    void showDescription() const override
    {
        std::cout << "from Derived: ";
        Base::showDescription();
    }

private:
    double* m_otherData;
};


int main()
{
    {
        auto baseSharedPtr = MakeSharedPtr<Base>("base type, instance # should be 1");
        assert(baseSharedPtr);
        baseSharedPtr->showDescription();
        std::cout << std::endl;

        auto derivedSharedPtr = MakeSharedPtr<Derived>("derived type, instance # should be 2");
        assert(derivedSharedPtr);
        derivedSharedPtr->showDescription();
        std::cout << std::endl;

        SharedPtr<Base> anotherBaseSharedPtr = MakeSharedPtr<Derived>(
                                       "derived type but stored as a base, instance # should be 3");
        assert(anotherBaseSharedPtr);
        anotherBaseSharedPtr->showDescription();
        std::cout << std::endl;

        {
            // creating this lifeExtension variable just so that we can see
            // the showDescription() call below before the destructors of anotherBaseSharedPtr
            //
            SharedPtr<Base> lifeExtensionForAnotherBaseSharedPtr{anotherBaseSharedPtr};
            assert(lifeExtensionForAnotherBaseSharedPtr);

            anotherBaseSharedPtr = baseSharedPtr;
            assert(baseSharedPtr);
            assert(anotherBaseSharedPtr);
            anotherBaseSharedPtr->showDescription();

            // now destructor for #3 should pop
        }

        std::cout << std::endl;
        anotherBaseSharedPtr = derivedSharedPtr;
        assert(derivedSharedPtr);
        assert(anotherBaseSharedPtr);
        anotherBaseSharedPtr->showDescription();
        std::cout << std::endl;

        anotherBaseSharedPtr = std::move(baseSharedPtr);
        assert(!baseSharedPtr);
        assert(anotherBaseSharedPtr);
        anotherBaseSharedPtr->showDescription();
        std::cout << std::endl;

        {
            // similarly as before
            //
            SharedPtr<Base> lifeExtensionForAnotherBaseSharedPtr{anotherBaseSharedPtr};
            assert(lifeExtensionForAnotherBaseSharedPtr);

            anotherBaseSharedPtr = std::move(derivedSharedPtr);
            assert(!derivedSharedPtr);
            assert(anotherBaseSharedPtr);
            anotherBaseSharedPtr->showDescription();

            // now destructor for #1 should pop
        }

        std::cout << std::endl;
        SharedPtr<Base> yetAnotherBaseSharedPtr{anotherBaseSharedPtr};
        assert(anotherBaseSharedPtr);
        assert(yetAnotherBaseSharedPtr);
        yetAnotherBaseSharedPtr->showDescription();

        SharedPtr<Base> lastBaseSharedPtr{std::move(yetAnotherBaseSharedPtr)};
        assert(!yetAnotherBaseSharedPtr);
        assert(lastBaseSharedPtr);
        lastBaseSharedPtr->showDescription();
        
        std::cout << "\noutter scope finished, destructors begin:" << std::endl;
    }

    assert(0 == Base::getCountOfAliveInstances());

    return 0;
}
