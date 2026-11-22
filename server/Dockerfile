FROM python:3.13-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY epaper.py fotolijst_server.py ./
USER nobody
EXPOSE 8080
CMD ["python", "fotolijst_server.py"]
