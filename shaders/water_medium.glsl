// Called for an underwater eye. A visible water interface already provides
// the exact front segment; otherwise an upward ray exits at the local plane.
float waterMediumDistance(float receiverDistance, bool interfaceHit,
                         float eyeHeight, float surfaceHeight, float rayY) {
    float distance = clamp(receiverDistance, 0.0, 24.0);
    if (!interfaceHit && rayY > 1.0e-6)
        distance = min(distance, max(surfaceHeight - eyeHeight, 0.0) / rayY);
    return distance;
}
