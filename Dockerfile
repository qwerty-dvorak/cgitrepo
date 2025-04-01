# ---------- Stage 1: Build ----------
    FROM gcc:latest AS builder
    WORKDIR /app
    
    # Copy only the necessary source code
    COPY main.c .
    
    # Compile the application
    RUN gcc -O2 -o server main.c
    
    # ---------- Stage 2: Runtime ----------
    FROM debian:bookworm-slim
    
    # Install only required runtime dependencies
    RUN apt-get update && apt-get install -y \
        git \
     && apt-get clean \
     && rm -rf /var/lib/apt/lists/*
    
    WORKDIR /app
    
    # Copy the compiled binary from the builder
    COPY --from=builder /app/server .
    
    # Expose the application port
    EXPOSE 3000
    
    # Run the server
    CMD ["./server"]
    