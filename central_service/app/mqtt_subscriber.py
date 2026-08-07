import os
import json
import logging
import asyncio
from datetime import datetime, timezone
import aiomqtt

from app.schemas import TelemetryPayload, MagneticData, GeoPoint, Diagnostics
from app.database import AsyncSessionLocal
from app.main import save_telemetry_sample

logger = logging.getLogger("mqtt_subscriber")

MQTT_HOST = os.getenv("MQTT_HOST", "mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "magnetometer/telemetry/#")

async def run_mqtt_subscriber():
    """Background task connecting to MQTT broker and ingesting telemetry payloads."""
    retry_interval = 5
    while True:
        try:
            logger.info(f"Connecting to MQTT broker at {MQTT_HOST}:{MQTT_PORT}...")
            async with aiomqtt.Client(MQTT_HOST, port=MQTT_PORT) as client:
                logger.info(f"Subscribing to topic '{MQTT_TOPIC}'...")
                await client.subscribe(MQTT_TOPIC)
                async for message in client.messages:
                    try:
                        payload_str = message.payload.decode("utf-8")
                        data = json.loads(payload_str)
                        
                        # Validate payload schema
                        telemetry = TelemetryPayload.model_validate(data)
                        
                        async with AsyncSessionLocal() as db:
                            await save_telemetry_sample(db, telemetry)
                            logger.debug(f"MQTT sample saved for node {telemetry.node_id}")
                    except Exception as parse_err:
                        logger.error(f"Failed to process MQTT message on {message.topic}: {parse_err}")
        except aiomqtt.MqttError as err:
            logger.warning(f"MQTT client connection error: {err}. Retrying in {retry_interval} seconds...")
            await asyncio.sleep(retry_interval)
        except Exception as e:
            logger.error(f"Unexpected MQTT error: {e}. Retrying in {retry_interval} seconds...")
            await asyncio.sleep(retry_interval)

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(run_mqtt_subscriber())
