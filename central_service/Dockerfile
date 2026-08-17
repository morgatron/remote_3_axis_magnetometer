FROM python:3.11-slim

WORKDIR /app

# Install curl for healthcheck
RUN apt-get update && apt-get install -y --no-install-recommends curl sqlite3 && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application files
COPY server.py gateway.py ./
COPY static/ ./static/
COPY stream_parser.py* ./

ENV DB_FILE=/app/data/magnetometer.db
ENV HOST=0.0.0.0
ENV PORT=8000

RUN mkdir -p /app/data

EXPOSE 8000
EXPOSE 9876/udp

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD curl -f http://localhost:8000/health || exit 1

CMD ["python", "server.py"]

