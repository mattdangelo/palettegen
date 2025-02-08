COMPOSE_FILE := docker-compose.yml

# Default target
.DEFAULT_GOAL := start

# Run the application (detached mode)
all:
	docker compose -f $(COMPOSE_FILE) --profile all up -d

# Run the application without the rails server
server-dev:
	docker compose -f $(COMPOSE_FILE) --profile server-dev up -d

# Stop the application
# TODO: Fix
stop:
	docker compose -f $(COMPOSE_FILE) stop
	docker compose -f $(COMPOSE_FILE) down --remove-orphans

# Restart the application
restart:
	docker compose -f $(COMPOSE_FILE) restart

# Bring down the application and remove containers
clean:
	docker compose -f $(COMPOSE_FILE) down

# Bring down the application, remove containers, and prune volumes
clean-all:
	docker compose -f $(COMPOSE_FILE) down --volumes

# Show running containers
ps:
	docker compose -f $(COMPOSE_FILE) ps

# Tail logs
logs:
	docker compose -f $(COMPOSE_FILE) logs -f

# Build images
build:
	docker compose -f $(COMPOSE_FILE) build

# Rebuild and restart containers
rebuild:
	docker compose -f $(COMPOSE_FILE) up -d --build

.PHONY: start stop restart clean clean-all ps logs build rebuild
