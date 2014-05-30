#pragma once

#include <cassert>
#include <vector>
#include <unordered_map>
#include "tool/config.h"
#include "component.h"

struct ComponentContainerData {
	size_t sizeOfComponent;
	size_t capacity;
	size_t freeIndex;
};


template<typename ComponentClass>
struct Components {
	const size_t size;
	ComponentClass*const components;


	ComponentClass& operator[](size_t index) {
		return components[index];
	}
};


class ComponentContainer {
public:
	ComponentContainer(Logger& logger, Configuration& config) : logger(logger), config(config) {
		initializeContainers();
        entityExistingTable.emplace_back(false); //null entity
	}


	~ComponentContainer() {
		for(auto& container : containers) {
			for(size_t i = 0; i < container.second.freeIndex; i++) {
				Component* currentComponent = (Component*)(container.first + i * container.second.sizeOfComponent);
				currentComponent->~Component();
			}
			free(container.first);
		}
	}


	bool componentExist(ComponentHandle component) {
		return componentHandles.find(component) != componentHandles.end();
	}


	template<typename ComponentClass>
	ComponentClass* getComponent(ComponentHandle component) {
        auto it = componentHandles.find(component);
        return it != componentHandles.end() ? (ComponentClass*)it->second : nullptr;
	}


	template<typename ComponentClass>
	ComponentClass* getComponent(Entity owner) {
		return findComponent<ComponentClass>(owner);
	}

    template<typename HeadComponentType, typename... TailComponents>
    void intersection(std::vector<HeadComponentType*>& head, std::vector<TailComponents*>&... tail) {
        auto& headContainer = containers[(size_t)HeadComponentType::type];
        HeadComponentType* headComponents = (HeadComponentType*)headContainer.first;

        for(size_t i = 0; i < headContainer.second.freeIndex; i++) {
            if(allComponentsExist(headComponents[i].owner, tail...)) {
                head.emplace_back(&headComponents[i]);
            }
        }
    }

	template<typename ComponentClass>
	Components<ComponentClass> getComponents() {
		return {containers[(size_t)ComponentClass::type].second.freeIndex, (ComponentClass*)containers[(size_t)ComponentClass::type].first};
	}


	template<typename ComponentClass, typename... CmpArgs>
	ComponentClass& createComponent(Entity owner, CmpArgs&&... cmpArgs) {
		assert(entityExist(owner));

		auto container = prepareComponentContainer<ComponentClass>();
		if(!container) {
			logger.fatal("Cannot prepare component container. Application will be terminated. Probably we don't have any memory.");
			exit(1);
		}

		char* adress = preparePlaceForNewComponent<ComponentClass>(owner);
		ComponentClass* createdComponent = new(adress) ComponentClass(owner, nextComponentHandle, std::forward<CmpArgs>(cmpArgs)...);

        componentHandles[nextComponentHandle] = createdComponent;
		nextComponentHandle++;
		container->second.freeIndex++;

		return *createdComponent;
	}


	template<typename ComponentClass>
	void deleteComponent(Entity owner) {
		assert(entityExist(owner));

		auto& container = containers[(size_t)ComponentClass::type];
		if(container.first == nullptr) {
			logger.warn("Cannot delete component type %d with owner %u: container don't exist", (int)ComponentClass::type, (unsigned int)owner);
			return;
		}

		ComponentClass* component = findComponent<ComponentClass>(owner);
		if(!component) {
			logger.warn("Cannot delete component type %d with owner %u: component not in container", (int)ComponentClass::type, (unsigned int)owner);
			return;
		}

		componentHandles.erase(component->handle);
		component->~ComponentClass();

		fillHoleAfterComponent(container, (char*)component);
		container.second.freeIndex--;

        size_t index = component - (ComponentClass*)container.first;
        refreshComponentHandles(container, index);
	}


	bool entityExist(Entity entityID) {
		return entityID < entityExistingTable.size() && entityExistingTable[entityID];
	}


	Entity createEntity() {
		entityExistingTable.push_back(true);
		return Entity(entityExistingTable.size() - 1);
	}


	void deleteEntity(Entity owner) {
		assert(entityExist(owner));

		for(auto& container : containers) {
			Component* currentComponent = (Component*)findComponent(owner, container);
			if(currentComponent) {
				componentHandles.erase(currentComponent->handle);
				currentComponent->~Component();
				fillHoleAfterComponent(container, (char*)currentComponent);
				container.second.freeIndex--;
                size_t index = ((char*)currentComponent - container.first) / container.second.sizeOfComponent;
                refreshComponentHandles(container, index);
			}
		}
		entityExistingTable[owner] = false;
	}


private:
	std::vector<char> entityExistingTable;

	std::vector<std::pair<char*, ComponentContainerData>> containers;
	std::unordered_map<ComponentHandle, Component*> componentHandles;
	ComponentHandle nextComponentHandle = 1;

    Logger& logger;
    Configuration& config;


	template<typename ComponentClass>
	std::pair<char*, ComponentContainerData>* prepareComponentContainer() {
		auto& container = containers[(size_t)ComponentClass::type];

		bool containerDontExist = container.first == nullptr;
		if(containerDontExist)
			return allocateNewContainer<ComponentClass>() ? &container : nullptr;

		bool containerIsFull = container.second.freeIndex == container.second.capacity;
		if(containerIsFull) {
            char* oldContainerAdress = container.first;
			if(!increaseContainerCapacity(container, ComponentClass::type))
				return nullptr;
            if(oldContainerAdress != container.first)
                refreshComponentHandles(container);
		}

		return &container;
	}


	void refreshComponentHandles(std::pair<char*, ComponentContainerData>& container, size_t startIndex = 0) {
		for(size_t i = startIndex; i < container.second.freeIndex; i++) {
			Component& elem = *(Component*)(container.first + i * container.second.sizeOfComponent);
			componentHandles[elem.handle] = &elem;
		}
	}


	bool increaseContainerCapacity(std::pair<char*, ComponentContainerData>& container, ComponentType typeID) {
		size_t newCapacity = container.second.capacity * config.get("componentContainer.growFactor", 16);
		char* newContainerAdress = (char*)realloc(container.first, newCapacity * container.second.sizeOfComponent);
		if(!newContainerAdress) {
			newCapacity = container.second.capacity + 1;
			newContainerAdress = (char*)realloc(container.first, newCapacity * container.second.sizeOfComponent);
			if(!newContainerAdress) {
				logger.fatal("Cannot resize component container, even by 1 element. Desired capacity: %d, Sizeof(Type): %d, type: %d", newCapacity, container.second.sizeOfComponent, typeID);
				assert(!"resizeContainer: cannot allocate memory for new element");
				return false;
			}
		}

		container.second.capacity = newCapacity;
		container.first = newContainerAdress;
		return true;
	}


	template<typename ComponentClass>
	bool allocateNewContainer() {
		ComponentContainerData metadata;
		metadata.capacity = config.get("componentContainer.initialCapacity", 4096);
		metadata.sizeOfComponent = sizeof(ComponentClass);
		metadata.freeIndex = 0;

		char* newContainer = (char*)malloc(sizeof(ComponentClass) * metadata.capacity);
		if(!newContainer) {
			metadata.capacity = 1;
			newContainer = (char*)malloc(sizeof(ComponentClass));
			if(!newContainer) {
				logger.fatal("Cannot create new container, even for 1 element. Sizeof(Type): %d, Type: %d", sizeof(ComponentClass), (int)ComponentClass::type);
				assert(!"Create component: cannot allocate memory for new container; even for 1 element");
				return false;
			}
		}

		containers[(size_t)ComponentClass::type] = {newContainer, metadata};
		return true;
	}


	template<typename ComponentClass>
	ComponentClass* findComponent(Entity owner) {
		auto& container = containers[(size_t)ComponentClass::type];
		ComponentClass* const components = (ComponentClass*)container.first;
		int min = 0;
		int max = container.second.freeIndex - 1;

		while(max >= min) {
			size_t mid = min + (max - min) / 2;
			if(components[mid].owner == owner) {
				return &components[mid];
			}
			if(components[mid].owner < owner) {
				min = mid + 1;
			}
			else {
				max = mid - 1;
			}
		}

		return nullptr;
	}


	char* findComponent(Entity owner, std::pair<char*, ComponentContainerData>& container) {   //TODO: binary search
		char* const components = container.first;

		for(size_t i = 0; i < container.second.freeIndex; i++) {
			Component* currentComponent = (Component*)(components + i * container.second.sizeOfComponent);
			if(currentComponent->owner == owner)
				return (char*)currentComponent;
		}
		return nullptr;
	}


	template<typename ComponentClass>
	char* preparePlaceForNewComponent(Entity owner) {
		auto& container = containers[(size_t)ComponentClass::type];

		if(container.second.freeIndex == 0) {
			return container.first;
		}

		ComponentClass* endOfContainer = (ComponentClass*)container.first + container.second.freeIndex;
		ComponentClass* lastComponent = endOfContainer - 1;
		if(lastComponent->owner > owner) {
			ComponentClass* place = findPlaceForNewComponent<ComponentClass>(owner);
			memmove(place + 1, place, (endOfContainer - place) * container.second.sizeOfComponent);
            
            size_t index = place + 1 - (ComponentClass*)container.first;
            refreshComponentHandles(container, index);
            componentHandles[endOfContainer->handle] = endOfContainer;

			return (char*)place;
		}

		return (char*)endOfContainer;
	}


	template<typename ComponentClass>
	ComponentClass* findPlaceForNewComponent(Entity owner) {        //TODO: sth. like binary search
		auto& container = containers[(size_t)ComponentClass::type];
		ComponentClass* const components = (ComponentClass*)container.first;

		size_t i;
		for(i = 0; i < container.second.freeIndex; i++) {
			if(components[i].owner > owner)
				return &components[i];
		}
		return &components[i];
	}


	void fillHoleAfterComponent(std::pair<char*, ComponentContainerData>& container, char* removedComponent) {
		char* endOfContainer = container.first + container.second.freeIndex * container.second.sizeOfComponent;
		if(removedComponent != (endOfContainer - container.second.sizeOfComponent)) {
			size_t bytesToMove = endOfContainer - removedComponent - container.second.sizeOfComponent;
			memmove(removedComponent, removedComponent + container.second.sizeOfComponent, bytesToMove);
		}
	}


	void initializeContainers() {
		ComponentContainerData null;
		null.capacity = 0;
		null.sizeOfComponent = 0;
		null.freeIndex = 0;

		for(unsigned int i = 0; i < (unsigned int)ComponentType::AmountOfComponentTypes; i++) {
			containers.emplace_back(nullptr, null);
		}
	}

    template<typename HeadComponentType, typename... TailComponents>
    bool allComponentsExist(Entity entity, std::vector<HeadComponentType*>& head, std::vector<TailComponents*>&... tail) {
        HeadComponentType* component = findComponent<HeadComponentType>(entity);
        if(!component) {
            return false;
        }

        if(allComponentsExist(tail...)) {
            head.emplace_back(component);
            return true;
        } else {
            return false;
        }
    }

    template<typename LastComponentType>
    bool allComponentsExist(Entity entity, std::vector<LastComponentType*>& last) {
        LastComponentType* component = findComponent<LastComponentType>(entity);
        if(!component) {
            return false;
        }

        last.emplace_back(component);
        return true;
    }
};

